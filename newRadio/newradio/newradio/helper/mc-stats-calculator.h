#ifndef MC_STATS_CALCULATOR_H_
#define MC_STATS_CALCULATOR_H_

#include "ns3/lte-common.h"
#include "ns3/uinteger.h"
#include "ns3/object.h"
#include "ns3/basic-data-calculators.h"
#include "ns3/lte-common.h"
#include <string>
#include <map>
#include <fstream>

namespace ns3 {

namespace newradio {

class McStatsCalculator : public Object
{
public:
  /**
   * Class constructor
   */
  McStatsCalculator ();

  /**
   * Class destructor
   */
  virtual
  ~McStatsCalculator ();

  // Inherited from ns3::Object
  /**
   *  Register this type.
   *  \return The object TypeId.
   */
  static TypeId GetTypeId (void);
  void DoDispose ();

  std::string GetLteOutputFilename (void);
  std::string GetnewradioOutputFilename (void);
  std::string GetCellIdInTimeOutputFilename (void);

  void SetLteOutputFilename (std::string outputFilename);
  void SetnewradioOutputFilename (std::string outputFilename);
  void SetCellIdInTimeOutputFilename (std::string outputFilename);

  void
  SwitchToLte (uint64_t imsi, uint16_t cellId, uint16_t rnti);

  void
  SwitchTonewradio (uint64_t imsi, uint16_t cellId, uint16_t rnti);

private:
  /**
   * Name of the file where the downlink PDCP statistics will be saved
   */
  std::string m_lteOutputFilename;

  /**
   * Name of the file where the uplink PDCP statistics will be saved
   */
  std::string m_newradioOutputFilename;

  std::string m_cellInTimeFilename;

  std::ofstream m_lteOutFile;
  std::ofstream m_newradioOutFile;
  std::ofstream m_cellInTimeOutFile;
};

} // namespace newradio

} // namespace ns3

#endif /* MC_STATS_CALCULATOR_H_ */
