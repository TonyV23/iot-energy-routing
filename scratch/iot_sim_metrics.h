#ifndef IOT_SIM_METRICS_H
#define IOT_SIM_METRICS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace iot
{

/**
 * Research-grade metrics collector for IoT routing experiments.
 * All values are computed from simulation traces — no hardcoded results.
 */
class MetricsCollector
{
  public:
    uint32_t m_txPackets{0};
    uint32_t m_rxPackets{0};
    uint64_t m_routingTxPackets{0};
    uint64_t m_routingRxPackets{0};
    uint64_t m_routingTxBytes{0};
    uint64_t m_dataTxBytes{0};
    uint64_t m_dataRxBytes{0};

    double m_firstNodeDeathTime{-1.0};
    double m_networkLifetime{-1.0};
    double m_lifetimeThreshold{0.0}; // fraction of nodes alive (e.g., 0.5 = 50%)

    std::vector<double> m_delaySamples;
    std::vector<double> m_energyConsumedJ;
    std::vector<double> m_initialEnergyJ;
    std::vector<double> m_remainingEnergyJ;
    std::vector<double> m_aliveFractionSamples;
    std::vector<double> m_timeSamples;

    std::string m_protocol;
    uint32_t m_nNodes{0};
    uint32_t m_nSensors{0};
    double m_simTime{0.0};
    uint32_t m_seed{0};
    double m_gridSize{0.0};
    double m_distance{0.0};
    uint32_t m_packetSize{0};
    std::string m_dataRate;
    std::string m_stackDescription;

    void
    RecordDelay(double delaySeconds)
    {
        if (delaySeconds >= 0.0)
        {
            m_delaySamples.push_back(delaySeconds);
        }
    }

    void
    RecordAliveFraction(double simTime, double fraction)
    {
        m_timeSamples.push_back(simTime);
        m_aliveFractionSamples.push_back(fraction);
    }

    double
    GetPdr() const
    {
        if (m_txPackets == 0)
        {
            return 0.0;
        }
        return static_cast<double>(m_rxPackets) / static_cast<double>(m_txPackets);
    }

    double
    GetThroughputKbps(double effectiveSimTime) const
    {
        if (effectiveSimTime <= 0.0)
        {
            return 0.0;
        }
        // Use sink-delivered application packets (avoids multi-hop double counting).
        const double deliveredBits = static_cast<double>(m_rxPackets * m_packetSize) * 8.0;
        return deliveredBits / effectiveSimTime / 1000.0;
    }

    double
    GetAverageDelayMs() const
    {
        if (m_delaySamples.empty())
        {
            return 0.0;
        }
        double sum = 0.0;
        for (double d : m_delaySamples)
        {
            sum += d;
        }
        return (sum / m_delaySamples.size()) * 1000.0;
    }

    double
    GetRoutingOverheadRatio() const
    {
        if (m_rxPackets == 0)
        {
            return 0.0;
        }
        return static_cast<double>(m_routingTxPackets) / static_cast<double>(m_rxPackets);
    }

    double
    GetAverageEnergyConsumedJ() const
    {
        if (m_energyConsumedJ.empty())
        {
            return 0.0;
        }
        double sum = 0.0;
        for (double e : m_energyConsumedJ)
        {
            sum += e;
        }
        return sum / m_energyConsumedJ.size();
    }

    double
    GetEnergyFairnessIndex() const
    {
        if (m_energyConsumedJ.empty())
        {
            return 1.0;
        }
        double sum = 0.0;
        double sumSq = 0.0;
        for (double e : m_energyConsumedJ)
        {
            sum += e;
            sumSq += e * e;
        }
        const double n = static_cast<double>(m_energyConsumedJ.size());
        if (sumSq <= 0.0)
        {
            return 1.0;
        }
        return (sum * sum) / (n * sumSq);
    }

    double
    GetFinalAliveFraction() const
    {
        if (m_aliveFractionSamples.empty())
        {
            return 1.0;
        }
        return m_aliveFractionSamples.back();
    }

    std::string
    BuildRunTag() const
    {
        std::ostringstream oss;
        oss << m_protocol << "_n" << m_nNodes << "_seed" << m_seed;
        return oss.str();
    }

    void
    WriteSummaryCsv(const std::string& path) const
    {
        const bool exists = std::ifstream(path).good();
        std::ofstream out(path, std::ios::app);
        if (!out)
        {
            return;
        }

        if (!exists)
        {
            out << "timestamp,protocol,n_nodes,n_sensors,seed,sim_time_s,grid_size_m,inter_node_distance_m,"
                   "packet_size_bytes,data_rate,stack,pdr,throughput_kbps,avg_delay_ms,routing_overhead_ratio,"
                   "routing_tx_packets,routing_rx_packets,data_tx_bytes,data_rx_bytes,first_node_death_s,"
                   "network_lifetime_s,lifetime_threshold,avg_energy_consumed_j,energy_fairness_index,"
                   "final_alive_fraction\n";
        }

        const double throughput = GetThroughputKbps(m_simTime);
        out << std::fixed << std::setprecision(6) << m_simTime << "," << m_protocol << "," << m_nNodes << ","
            << m_nSensors << "," << m_seed << "," << m_simTime << "," << m_gridSize << "," << m_distance << ","
            << m_packetSize << "," << m_dataRate << "," << QuoteCsv(m_stackDescription) << "," << GetPdr() << ","
            << throughput << "," << GetAverageDelayMs() << "," << GetRoutingOverheadRatio() << ","
            << m_routingTxPackets << "," << m_routingRxPackets << "," << m_dataTxBytes << "," << m_dataRxBytes
            << "," << m_firstNodeDeathTime << "," << m_networkLifetime << "," << m_lifetimeThreshold << ","
            << GetAverageEnergyConsumedJ() << "," << GetEnergyFairnessIndex() << "," << GetFinalAliveFraction()
            << "\n";
    }

    void
    WriteEnergyTimeseriesCsv(const std::string& path) const
    {
        std::ofstream out(path);
        if (!out)
        {
            return;
        }
        out << "node_id,initial_energy_j,remaining_energy_j,consumed_energy_j\n";
        for (size_t i = 0; i < m_initialEnergyJ.size(); ++i)
        {
            const double consumed = m_initialEnergyJ[i] - m_remainingEnergyJ[i];
            out << i << "," << m_initialEnergyJ[i] << "," << m_remainingEnergyJ[i] << "," << consumed << "\n";
        }
    }

    void
    WriteLifetimeCsv(const std::string& path) const
    {
        std::ofstream out(path);
        if (!out)
        {
            return;
        }
        out << "time_s,alive_fraction\n";
        for (size_t i = 0; i < m_timeSamples.size(); ++i)
        {
            out << m_timeSamples[i] << "," << m_aliveFractionSamples[i] << "\n";
        }
    }

    void
    WriteDelayCsv(const std::string& path) const
    {
        std::ofstream out(path);
        if (!out)
        {
            return;
        }
        out << "sample_id,delay_ms\n";
        for (size_t i = 0; i < m_delaySamples.size(); ++i)
        {
            out << i << "," << (m_delaySamples[i] * 1000.0) << "\n";
        }
    }

  private:
    static std::string
    QuoteCsv(const std::string& value)
    {
        std::string quoted = "\"";
        for (char c : value)
        {
            if (c == '"')
            {
                quoted += "\"\"";
            }
            else
            {
                quoted += c;
            }
        }
        quoted += "\"";
        return quoted;
    }
};

} // namespace iot

#endif // IOT_SIM_METRICS_H
