/**
 * iot_energy_sim.cc
 *
 * Energy-Efficient Routing Protocols in Large-Scale IoT Deployments
 * NS-3.41 compatible simulation for academic research.
 *
 * Protocols:
 *   AODV / DSR  — WiFi 802.11 ad-hoc MANET (common IoT gateway mesh literature)
 *   RPL         — IEEE 802.15.4 + 6LoWPAN LLN stack (stock NS-3 has no native RPL;
 *                 see docs/methodology.md for academic justification)
 */

#include "iot_sim_metrics.h"

#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/dsr-helper.h"
#include "ns3/dsr-main-helper.h"
#include "ns3/dsr-module.h"
#include "ns3/energy-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/sixlowpan-module.h"
#include "ns3/wifi-module.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("IotEnergySim");

namespace
{

constexpr uint16_t APP_PORT = 5000;
constexpr uint16_t AODV_PORT = 654;
constexpr double LIFETIME_FRACTION_THRESHOLD = 0.5;
constexpr double ENERGY_SAMPLE_INTERVAL_S = 1.0;

struct SimConfig
{
    std::string protocol{"AODV"};
    uint32_t nNodes{50};
    double simTime{120.0};
    uint32_t packetSize{512};
    std::string dataRate{"32kb/s"};
    double gridSize{250.0};
    double distance{25.0};
    uint32_t seed{1};
    std::string outputDir{"results/csv"};
    double initialEnergyJ{10000.0};
    bool verbose{false};
};

iot::MetricsCollector g_metrics;
SimConfig g_config;
std::vector<bool> g_nodeDead;
std::vector<Ptr<BasicEnergySource>> g_energySources;
std::vector<double> g_manualEnergyConsumedJ;
Ptr<FlowMonitor> g_flowMonitor;
FlowMonitorHelper g_flowHelper;
std::string g_runTag;

std::string
ToUpper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

bool
EnsureDirectory(const std::string& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}

uint32_t
ComputeGridWidth(uint32_t nNodes, double gridSize, double distance)
{
    if (distance <= 0.0)
    {
        distance = 1.0;
    }
    const uint32_t fromArea = static_cast<uint32_t>(std::max(1.0, std::floor(gridSize / distance) + 1.0));
    const uint32_t fromSqrt = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(nNodes))));
    return std::max(fromSqrt, fromArea);
}

void
ValidateConfig(const SimConfig& cfg)
{
    NS_ABORT_MSG_IF(cfg.nNodes < 2, "nNodes must be >= 2 (1 sink + >= 1 sensor)");
    NS_ABORT_MSG_IF(cfg.simTime <= 0.0, "simTime must be positive");
    NS_ABORT_MSG_IF(cfg.packetSize < 64, "packetSize must be >= 64 bytes");
    NS_ABORT_MSG_IF(cfg.gridSize <= 0.0, "gridSize must be positive");
    NS_ABORT_MSG_IF(cfg.distance <= 0.0, "distance must be positive");
    NS_ABORT_MSG_IF(cfg.initialEnergyJ <= 0.0, "initialEnergyJ must be positive");

    const std::string proto = ToUpper(cfg.protocol);
    NS_ABORT_MSG_IF(proto != "AODV" && proto != "DSR" && proto != "RPL",
                    "protocol must be AODV, DSR, or RPL");
}

bool
IsApplicationPacket(Ptr<const Packet> packet, const Ipv4Header& header)
{
    if (header.GetProtocol() != 17)
    {
        return false;
    }
    Ptr<Packet> copy = packet->Copy();
    copy->RemoveHeader(const_cast<Ipv4Header&>(header));
    UdpHeader udp;
    if (!copy->PeekHeader(udp))
    {
        return false;
    }
    return udp.GetDestinationPort() == APP_PORT || udp.GetSourcePort() == APP_PORT;
}

bool
IsRoutingPacketWifi(Ptr<const Packet> packet, const Ipv4Header& header, const std::string& protocol)
{
    if (IsApplicationPacket(packet, header))
    {
        return false;
    }

    if (protocol == "AODV")
    {
        if (header.GetProtocol() != 17)
        {
            return false;
        }
        Ptr<Packet> copy = packet->Copy();
        copy->RemoveHeader(const_cast<Ipv4Header&>(header));
        UdpHeader udp;
        if (!copy->PeekHeader(udp))
        {
            return false;
        }
        return udp.GetDestinationPort() == AODV_PORT || udp.GetSourcePort() == AODV_PORT;
    }

    if (protocol == "DSR")
    {
        // DSR control traffic is not on the application port; exclude TCP/ICMP if present.
        return header.GetProtocol() != 6 && header.GetProtocol() != 1;
    }

    return false;
}

void
Ipv4TxTraceWifiImpl(const std::string& protocol,
                    Ptr<const Packet> packet,
                    Ptr<Ipv4> ipv4,
                    uint32_t interface)
{
    (void)ipv4;
    (void)interface;

    Ipv4Header header;
    Ptr<Packet> copy = packet->Copy();
    if (!copy->PeekHeader(header))
    {
        return;
    }

    if (IsRoutingPacketWifi(copy, header, protocol))
    {
        g_metrics.m_routingTxPackets++;
        g_metrics.m_routingTxBytes += packet->GetSize();
    }
    else if (IsApplicationPacket(copy, header))
    {
        g_metrics.m_dataTxBytes += packet->GetSize();
    }
}

void
Ipv4RxTraceWifiImpl(const std::string& protocol,
                    Ptr<const Packet> packet,
                    Ptr<Ipv4> ipv4,
                    uint32_t interface)
{
    (void)ipv4;
    (void)interface;

    Ipv4Header header;
    Ptr<Packet> copy = packet->Copy();
    if (!copy->PeekHeader(header))
    {
        return;
    }

    if (IsRoutingPacketWifi(copy, header, protocol))
    {
        g_metrics.m_routingRxPackets++;
    }
    else if (IsApplicationPacket(copy, header))
    {
        g_metrics.m_dataRxBytes += packet->GetSize();
    }
}

void
Ipv4TxTraceWifi(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    Ipv4TxTraceWifiImpl(g_metrics.m_protocol, packet, ipv4, interface);
}

void
Ipv4RxTraceWifi(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    Ipv4RxTraceWifiImpl(g_metrics.m_protocol, packet, ipv4, interface);
}

void
AppTxTrace(Ptr<const Packet> packet)
{
    g_metrics.m_txPackets++;
}

void
AppRxTrace(Ptr<const Packet> packet, const Address& address)
{
    (void)address;
    g_metrics.m_rxPackets++;
}

void
SampleEnergyAndLifetime(NodeContainer nodes)
{
    uint32_t alive = 0;
    const uint32_t n = nodes.GetN();
    const double now = Simulator::Now().GetSeconds();

    for (uint32_t i = 0; i < n; ++i)
    {
        Ptr<BasicEnergySource> source = g_energySources[i];
        if (!source)
        {
            continue;
        }

        double consumed = g_metrics.m_initialEnergyJ[i] - source->GetRemainingEnergy();
        if (i < g_manualEnergyConsumedJ.size() && g_manualEnergyConsumedJ[i] > 0.0)
        {
            // LLN path: BasicEnergySource has no LR-WPAN device model; use manual radio accounting.
            consumed = g_manualEnergyConsumedJ[i];
            // Add idle consumption for 802.15.4 (~0.42 mA @ 3 V).
            consumed += 3.0 * 0.00042 * now;
        }
        g_metrics.m_energyConsumedJ[i] = consumed;
        const double remaining = std::max(0.0, g_metrics.m_initialEnergyJ[i] - consumed);
        g_metrics.m_remainingEnergyJ[i] = remaining;

        if (remaining <= 0.0)
        {
            if (!g_nodeDead[i])
            {
                g_nodeDead[i] = true;
                if (g_metrics.m_firstNodeDeathTime < 0.0)
                {
                    g_metrics.m_firstNodeDeathTime = Simulator::Now().GetSeconds();
                }
            }
        }
        else
        {
            alive++;
        }
    }

    const double aliveFraction = static_cast<double>(alive) / static_cast<double>(n);
    g_metrics.RecordAliveFraction(now, aliveFraction);

    if (g_metrics.m_networkLifetime < 0.0 && aliveFraction <= LIFETIME_FRACTION_THRESHOLD)
    {
        g_metrics.m_networkLifetime = now;
    }

    if (now + ENERGY_SAMPLE_INTERVAL_S < g_config.simTime)
    {
        Simulator::Schedule(Seconds(ENERGY_SAMPLE_INTERVAL_S),
                            &SampleEnergyAndLifetime,
                            nodes);
    }
}

void
InstallWifiEnergyModel(NodeContainer nodes, NetDeviceContainer devices)
{
    BasicEnergySourceHelper sourceHelper;
    sourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(g_config.initialEnergyJ));
    sourceHelper.Set("PeriodicEnergyUpdateInterval", TimeValue(Seconds(ENERGY_SAMPLE_INTERVAL_S)));

    WifiRadioEnergyModelHelper radioHelper;
    // IoT-oriented WiFi module current draw (ESP8266-class order of magnitude, 3.3 V supply).
    radioHelper.Set("TxCurrentA", DoubleValue(0.170));
    radioHelper.Set("RxCurrentA", DoubleValue(0.060));
    radioHelper.Set("IdleCurrentA", DoubleValue(0.015));
    radioHelper.Set("CcaBusyCurrentA", DoubleValue(0.015));
    radioHelper.Set("SwitchingCurrentA", DoubleValue(0.015));

    EnergySourceContainer sources = sourceHelper.Install(nodes);
    DeviceEnergyModelContainer models = radioHelper.Install(devices, sources);

    (void)models;

    g_energySources.resize(nodes.GetN());
    g_metrics.m_initialEnergyJ.assign(nodes.GetN(), g_config.initialEnergyJ);
    g_metrics.m_remainingEnergyJ.assign(nodes.GetN(), g_config.initialEnergyJ);
    g_metrics.m_energyConsumedJ.assign(nodes.GetN(), 0.0);
    g_nodeDead.assign(nodes.GetN(), false);

    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        g_energySources[i] = DynamicCast<BasicEnergySource>(sources.Get(i));
    }
}

void
ConsumeLlnRadioEnergy(uint32_t nodeId, double joules)
{
    if (nodeId >= g_manualEnergyConsumedJ.size())
    {
        return;
    }
    g_manualEnergyConsumedJ[nodeId] += joules;
}

void
LrWpanMacTxTrace(uint32_t nodeId, Ptr<const Packet> packet)
{
    // CC2420 datasheet order-of-magnitude: 17.4 mA TX @ 3.0 V.
    const double voltage = 3.0;
    const double txCurrent = 0.0174;
    const double duration = std::max(0.001, packet->GetSize() * 8.0 / 250000.0);
    ConsumeLlnRadioEnergy(nodeId, voltage * txCurrent * duration);
}

void
LrWpanMacRxTrace(uint32_t nodeId, Ptr<const Packet> packet)
{
    // CC2420 RX current ~19.7 mA @ 3.0 V.
    const double voltage = 3.0;
    const double rxCurrent = 0.0197;
    const double duration = std::max(0.001, packet->GetSize() * 8.0 / 250000.0);
    ConsumeLlnRadioEnergy(nodeId, voltage * rxCurrent * duration);
}

void
InstallLrWpanEnergyModel(NodeContainer nodes)
{
    BasicEnergySourceHelper sourceHelper;
    sourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(g_config.initialEnergyJ));
    sourceHelper.Set("PeriodicEnergyUpdateInterval", TimeValue(Seconds(ENERGY_SAMPLE_INTERVAL_S)));

    EnergySourceContainer sources = sourceHelper.Install(nodes);

    g_energySources.resize(nodes.GetN());
    g_metrics.m_initialEnergyJ.assign(nodes.GetN(), g_config.initialEnergyJ);
    g_metrics.m_remainingEnergyJ.assign(nodes.GetN(), g_config.initialEnergyJ);
    g_metrics.m_energyConsumedJ.assign(nodes.GetN(), 0.0);
    g_manualEnergyConsumedJ.assign(nodes.GetN(), 0.0);
    g_nodeDead.assign(nodes.GetN(), false);

    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        g_energySources[i] = DynamicCast<BasicEnergySource>(sources.Get(i));

        Ptr<SixLowPanNetDevice> sixDev =
            DynamicCast<SixLowPanNetDevice>(nodes.Get(i)->GetDevice(0));
        if (!sixDev)
        {
            continue;
        }
        Ptr<LrWpanNetDevice> dev =
            DynamicCast<LrWpanNetDevice>(sixDev->GetNetDevice());
        if (!dev)
        {
            continue;
        }

        dev->GetMac()->TraceConnectWithoutContext(
            "MacTx",
            MakeBoundCallback(&LrWpanMacTxTrace, i));
        dev->GetMac()->TraceConnectWithoutContext(
            "MacRx",
            MakeBoundCallback(&LrWpanMacRxTrace, i));
    }

    (void)sources;
}

void
CollectFlowMonitorStats()
{
    if (!g_flowMonitor)
    {
        return;
    }

    g_flowMonitor->CheckForLostPackets();

    for (auto iter = g_flowMonitor->GetFlowStats().begin();
         iter != g_flowMonitor->GetFlowStats().end();
         ++iter)
    {
        const FlowMonitor::FlowStats& stats = iter->second;
        if (stats.rxPackets == 0)
        {
            continue;
        }

        const double avgDelay = stats.delaySum.GetSeconds() / static_cast<double>(stats.rxPackets);
        g_metrics.RecordDelay(avgDelay);
    }
}

void
WriteOutputs()
{
    EnsureDirectory(g_config.outputDir);
    EnsureDirectory("results/logs");

    const std::string summaryPath = g_config.outputDir + "/summary.csv";
    g_metrics.WriteSummaryCsv(summaryPath);
    g_metrics.WriteEnergyTimeseriesCsv(g_config.outputDir + "/energy_" + g_runTag + ".csv");
    g_metrics.WriteLifetimeCsv(g_config.outputDir + "/lifetime_" + g_runTag + ".csv");
    g_metrics.WriteDelayCsv(g_config.outputDir + "/delay_" + g_runTag + ".csv");

    std::ofstream logFile("results/logs/" + g_runTag + ".log");
    if (logFile)
    {
        logFile << "protocol=" << g_metrics.m_protocol << "\n";
        logFile << "pdr=" << g_metrics.GetPdr() << "\n";
        logFile << "throughput_kbps=" << g_metrics.GetThroughputKbps(g_config.simTime) << "\n";
        logFile << "avg_delay_ms=" << g_metrics.GetAverageDelayMs() << "\n";
        logFile << "routing_overhead_ratio=" << g_metrics.GetRoutingOverheadRatio() << "\n";
        logFile << "first_node_death_s=" << g_metrics.m_firstNodeDeathTime << "\n";
        logFile << "network_lifetime_s=" << g_metrics.m_networkLifetime << "\n";
        logFile << "energy_fairness=" << g_metrics.GetEnergyFairnessIndex() << "\n";
    }

    std::cout << "=== IoT Energy Routing Simulation Results ===" << std::endl;
    std::cout << "Protocol: " << g_metrics.m_protocol << " (" << g_metrics.m_stackDescription << ")"
              << std::endl;
    std::cout << "Nodes: " << g_metrics.m_nNodes << " (sensors: " << g_metrics.m_nSensors << ")"
              << std::endl;
    std::cout << "PDR: " << g_metrics.GetPdr() << std::endl;
    std::cout << "Throughput (kbps): " << g_metrics.GetThroughputKbps(g_config.simTime) << std::endl;
    std::cout << "Avg Delay (ms): " << g_metrics.GetAverageDelayMs() << std::endl;
    std::cout << "Routing Overhead Ratio: " << g_metrics.GetRoutingOverheadRatio() << std::endl;
    std::cout << "First Node Death (s): " << g_metrics.m_firstNodeDeathTime << std::endl;
    std::cout << "Network Lifetime 50% (s): " << g_metrics.m_networkLifetime << std::endl;
    std::cout << "Energy Fairness (Jain): " << g_metrics.GetEnergyFairnessIndex() << std::endl;
    std::cout << "CSV summary: " << summaryPath << std::endl;
}

void
InstallManyToOneTraffic(NodeContainer nodes, Ipv4Address sinkAddress, double startJitter)
{
    const uint16_t port = APP_PORT;

    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sinkHelper.Install(nodes.Get(0));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(g_config.simTime));

    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApp.Get(0));
    if (sink)
    {
        sink->TraceConnectWithoutContext("Rx", MakeCallback(&AppRxTrace));
    }

    for (uint32_t i = 1; i < nodes.GetN(); ++i)
    {
        OnOffHelper onoff("ns3::UdpSocketFactory",
                          InetSocketAddress(sinkAddress, port));
        onoff.SetAttribute("PacketSize", UintegerValue(g_config.packetSize));
        onoff.SetAttribute("DataRate", StringValue(g_config.dataRate));
        onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

        ApplicationContainer apps = onoff.Install(nodes.Get(i));
        const double start = 1.0 + startJitter * static_cast<double>(i);
        apps.Start(Seconds(start));
        apps.Stop(Seconds(g_config.simTime - 1.0));

        apps.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&AppTxTrace));
    }
}

void
RunWifiManetSimulation(const std::string& protocol)
{
    NodeContainer nodes;
    nodes.Create(g_config.nNodes);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("DsssRate1Mbps"),
                                 "ControlMode",
                                 StringValue("DsssRate1Mbps"));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");

    YansWifiPhyHelper phy;
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    phy.SetChannel(channel.Create());
    phy.Set("RxGain", DoubleValue(0.0));
    phy.Set("TxPowerStart", DoubleValue(7.0));
    phy.Set("TxPowerEnd", DoubleValue(7.0));

    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    MobilityHelper mobility;
    const uint32_t gridWidth = ComputeGridWidth(g_config.nNodes, g_config.gridSize, g_config.distance);
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX",
                                  DoubleValue(0.0),
                                  "MinY",
                                  DoubleValue(0.0),
                                  "DeltaX",
                                  DoubleValue(g_config.distance),
                                  "DeltaY",
                                  DoubleValue(g_config.distance),
                                  "GridWidth",
                                  UintegerValue(gridWidth),
                                  "LayoutType",
                                  StringValue("RowFirst"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    InstallWifiEnergyModel(nodes, devices);

    InternetStackHelper internet;

    if (protocol == "AODV")
    {
        AodvHelper aodv;
        Ipv4ListRoutingHelper list;
        list.Add(aodv, 100);
        internet.SetRoutingHelper(list);
        internet.Install(nodes);
    }
    else if (protocol == "DSR")
    {
        internet.Install(nodes);
        DsrHelper dsr;
        DsrMainHelper dsrMain;
        dsrMain.Install(dsr, nodes);
    }

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    Config::ConnectWithoutContext("/NodeList/*/$ns3::Ipv4L3Protocol/Tx",
                                  MakeCallback(&Ipv4TxTraceWifi));
    Config::ConnectWithoutContext("/NodeList/*/$ns3::Ipv4L3Protocol/Rx",
                                  MakeCallback(&Ipv4RxTraceWifi));

    InstallManyToOneTraffic(nodes, interfaces.GetAddress(0), 0.05);

    // FlowMonitor is incompatible with DSR in NS-3 (see manet-routing-compare example).
    if (protocol != "DSR")
    {
        g_flowMonitor = g_flowHelper.InstallAll();
    }

    g_metrics.m_stackDescription = "WiFi 802.11b ad-hoc MANET";
    g_metrics.m_lifetimeThreshold = LIFETIME_FRACTION_THRESHOLD;

    Simulator::Schedule(Seconds(1.0), &SampleEnergyAndLifetime, nodes);
    Simulator::Stop(Seconds(g_config.simTime));
    Simulator::Run();

    CollectFlowMonitorStats();
    SampleEnergyAndLifetime(nodes);
    Simulator::Destroy();
}

void
SixLowPanTxTrace(Ptr<const Packet> packet, Ptr<SixLowPanNetDevice> device, uint32_t ifIndex)
{
    (void)device;
    (void)ifIndex;
    g_metrics.m_routingTxPackets++;
    g_metrics.m_routingTxBytes += packet->GetSize();
}

void
SixLowPanRxTrace(Ptr<const Packet> packet, Ptr<SixLowPanNetDevice> device, uint32_t ifIndex)
{
    (void)packet;
    (void)device;
    (void)ifIndex;
    g_metrics.m_routingRxPackets++;
}

void
RunLlnRplProxySimulation()
{
    /**
     * RPL in stock NS-3.41 is NOT available as a native module.
     * This path uses IEEE 802.15.4 + 6LoWPAN mesh-under routing toward the sink,
     * which is the standard academically accepted LLN IoT stack proxy documented
     * in docs/methodology.md. Results are labeled RPL-proxy / LLN, not full RPL.
     */

    Ipv6AddressGenerator::Reset();

    NodeContainer nodes;
    nodes.Create(g_config.nNodes);

    MobilityHelper mobility;
    const uint32_t gridWidth = ComputeGridWidth(g_config.nNodes, g_config.gridSize, g_config.distance);
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX",
                                  DoubleValue(0.0),
                                  "MinY",
                                  DoubleValue(0.0),
                                  "DeltaX",
                                  DoubleValue(g_config.distance),
                                  "DeltaY",
                                  DoubleValue(g_config.distance),
                                  "GridWidth",
                                  UintegerValue(gridWidth),
                                  "LayoutType",
                                  StringValue("RowFirst"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    LrWpanHelper lrWpan;
    NetDeviceContainer lrwpanDevices = lrWpan.Install(nodes);
    lrWpan.CreateAssociatedPan(lrwpanDevices, 0);

    InstallLrWpanEnergyModel(nodes);

    InternetStackHelper internet;
    internet.Install(nodes);

    SixLowPanHelper sixLowPan;
    sixLowPan.SetDeviceAttribute("UseMeshUnder", BooleanValue(true));
    const uint8_t meshRadius =
        static_cast<uint8_t>(std::min<uint32_t>(255U, gridWidth));
    sixLowPan.SetDeviceAttribute("MeshUnderRadius", UintegerValue(meshRadius));
    NetDeviceContainer sixDevices = sixLowPan.Install(lrwpanDevices);

    Ipv6AddressHelper ipv6;
    ipv6.SetBase(Ipv6Address("2001:f00d::"), Ipv6Prefix(64));
    Ipv6InterfaceContainer interfaces = ipv6.Assign(sixDevices);
    interfaces.SetForwarding(0, true);

    // Many-to-one UDP/IPv6 traffic toward sink (node 0).
    const uint16_t port = APP_PORT;
    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                Inet6SocketAddress(Ipv6Address::GetAny(), port));
    ApplicationContainer sinkApp = sinkHelper.Install(nodes.Get(0));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(g_config.simTime));

    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApp.Get(0));
    if (sink)
    {
        sink->TraceConnectWithoutContext("Rx", MakeCallback(&AppRxTrace));
    }

    Ipv6Address sinkAddress = interfaces.GetAddress(0, 1);

    for (uint32_t i = 1; i < nodes.GetN(); ++i)
    {
        OnOffHelper onoff("ns3::UdpSocketFactory", Inet6SocketAddress(sinkAddress, port));
        onoff.SetAttribute("PacketSize", UintegerValue(g_config.packetSize));
        onoff.SetAttribute("DataRate", StringValue(g_config.dataRate));
        onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

        ApplicationContainer apps = onoff.Install(nodes.Get(i));
        apps.Start(Seconds(1.0 + 0.05 * static_cast<double>(i)));
        apps.Stop(Seconds(g_config.simTime - 1.0));
        apps.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&AppTxTrace));
    }

    // Mesh-under forwarding counts as LLN routing overhead (not full RPL DIO/DAO).
    for (uint32_t i = 1; i < nodes.GetN(); ++i)
    {
        Ptr<SixLowPanNetDevice> sixDev =
            DynamicCast<SixLowPanNetDevice>(nodes.Get(i)->GetDevice(0));
        if (!sixDev)
        {
            continue;
        }
        sixDev->TraceConnectWithoutContext("Tx", MakeCallback(&SixLowPanTxTrace));
        sixDev->TraceConnectWithoutContext("Rx", MakeCallback(&SixLowPanRxTrace));
    }

    g_flowMonitor = g_flowHelper.InstallAll();

    g_metrics.m_stackDescription =
        "802.15.4 + 6LoWPAN mesh-under (RPL proxy — not native RPL in NS-3.41)";
    g_metrics.m_lifetimeThreshold = LIFETIME_FRACTION_THRESHOLD;

    Simulator::Schedule(Seconds(1.0), &SampleEnergyAndLifetime, nodes);
    Simulator::Stop(Seconds(g_config.simTime));
    Simulator::Run();

    CollectFlowMonitorStats();
    SampleEnergyAndLifetime(nodes);
    Simulator::Destroy();
}

} // namespace

int
main(int argc, char* argv[])
{
    SimConfig cfg;
    CommandLine cmd(__FILE__);
    cmd.AddValue("protocol", "Routing protocol: AODV, DSR, or RPL", cfg.protocol);
    cmd.AddValue("nNodes", "Total nodes (1 sink + N-1 sensors)", cfg.nNodes);
    cmd.AddValue("simTime", "Simulation time in seconds", cfg.simTime);
    cmd.AddValue("packetSize", "Application packet size in bytes", cfg.packetSize);
    cmd.AddValue("dataRate", "Sensor data rate (e.g. 32kb/s)", cfg.dataRate);
    cmd.AddValue("gridSize", "Deployment area width/height in meters", cfg.gridSize);
    cmd.AddValue("distance", "Inter-node grid spacing in meters", cfg.distance);
    cmd.AddValue("seed", "RNG seed for reproducibility", cfg.seed);
    cmd.AddValue("outputDir", "Directory for CSV output", cfg.outputDir);
    cmd.AddValue("initialEnergyJ", "Initial battery energy per node (Joules)", cfg.initialEnergyJ);
    cmd.AddValue("verbose", "Enable NS_LOG debug output", cfg.verbose);
    cmd.Parse(argc, argv);

    cfg.protocol = ToUpper(cfg.protocol);
    ValidateConfig(cfg);
    g_config = cfg;

    if (cfg.verbose)
    {
        LogComponentEnable("IotEnergySim", LOG_LEVEL_INFO);
        LogComponentEnable("AodvRoutingProtocol", LOG_LEVEL_WARN);
        LogComponentEnable("DsrRouting", LOG_LEVEL_WARN);
    }

    RngSeedManager::SetSeed(cfg.seed);
    RngSeedManager::SetRun(1);

    g_metrics = iot::MetricsCollector{};
    g_metrics.m_protocol = cfg.protocol;
    g_metrics.m_nNodes = cfg.nNodes;
    g_metrics.m_nSensors = cfg.nNodes - 1;
    g_metrics.m_simTime = cfg.simTime;
    g_metrics.m_seed = cfg.seed;
    g_metrics.m_gridSize = cfg.gridSize;
    g_metrics.m_distance = cfg.distance;
    g_metrics.m_packetSize = cfg.packetSize;
    g_metrics.m_dataRate = cfg.dataRate;
    g_runTag = g_metrics.BuildRunTag();

    if (cfg.protocol == "RPL")
    {
        RunLlnRplProxySimulation();
    }
    else
    {
        RunWifiManetSimulation(cfg.protocol);
    }

    WriteOutputs();
    return 0;
}
