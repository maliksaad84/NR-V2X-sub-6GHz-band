#ifndef newradio_CHANNEL_RAYTRACING_H_
#define newradio_CHANNEL_RAYTRACING_H_


#include <ns3/spectrum-value.h>
#include <complex>
#include <ns3/spectrum-signal-parameters.h>
#include <ns3/mobility-model.h>
#include <ns3/spectrum-propagation-loss-model.h>
#include <ns3/net-device.h>
#include <map>
#include <ns3/angles.h>
#include <ns3/net-device-container.h>
#include <ns3/random-variable-stream.h>
#include "newradio-phy-mac-common.h"




namespace ns3 {

namespace newradio {


typedef std::vector<double> doubleVector_t;
typedef std::vector< std::complex<double> > complexVector_t;
typedef std::vector<complexVector_t> complex2DVector_t;
typedef std::pair<Ptr<NetDevice>, Ptr<NetDevice> > key_t;


struct TraceParams : public SimpleRefCount<TraceParams>
{
  complex2DVector_t       m_txSpatialMatrix;       // tx side spatial matrix
  complex2DVector_t       m_rxSpatialMatrix;       // rx side spatial matrix
  doubleVector_t          m_powerFraction;       // store subpath power fraction
  doubleVector_t          m_delaySpread;       // store delay spread
  doubleVector_t          m_doppler;       // store doppler



};

struct newradioBeamFormingTraces : public SimpleRefCount<newradioBeamFormingTraces>
{
  complexVector_t                 m_txW;       // tx antenna weights
  complexVector_t                 m_rxW;       // rx antenna weights
  Ptr<TraceParams>        m_channelParams;
};


class newradioChannelRaytracing : public SpectrumPropagationLossModel
{
public:
  newradioChannelRaytracing ();
  virtual ~newradioChannelRaytracing ();

  static TypeId GetTypeId (void);
  void DoDispose ();
  void LoadTraces ();
  void ConnectDevices (Ptr<NetDevice> dev1, Ptr<NetDevice> dev2);
  void Initial (NetDeviceContainer ueDevices, NetDeviceContainer enbDevices);

  void SetConfigurationParameters (Ptr<newradioPhyMacCommon> ptrConfig);
  Ptr<newradioPhyMacCommon> GetConfigurationParameters (void) const;

  // hack to allow newradioEnbPhy to compute SINR even without pilots
  Ptr<SpectrumValue> CalcRxPowerSpectralDensity (Ptr<const SpectrumValue> txPsd,
                                                 Ptr<const MobilityModel> a,
                                                 Ptr<const MobilityModel> b) const;

private:
  Ptr<SpectrumValue> DoCalcRxPowerSpectralDensity (Ptr<const SpectrumValue> txPsd,
                                                   Ptr<const MobilityModel> a,
                                                   Ptr<const MobilityModel> b) const;

  complex2DVector_t GenSpatialMatrix (uint64_t traceIndex, uint16_t* antennaNum, bool bs) const;
  complexVector_t GenSinglePath (double hAngle, double vAngle, uint16_t* antennaNum) const;
  complexVector_t CalcBeamformingVector (complex2DVector_t SpatialMatrix, doubleVector_t powerFraction) const;
  Ptr<SpectrumValue> GetChannelGain (Ptr<const SpectrumValue> txPsd, Ptr<newradioBeamFormingTraces> bfParams, double speed) const;
  double GetSystemBandwidth () const;
  void SetBeamformingVector (Ptr<NetDevice> ueDevice, Ptr<NetDevice> enbDevice);

  mutable std::map< key_t, int > m_connectedPair;
  mutable std::map< key_t, Ptr<TraceParams> > m_channelMatrixMap;
  double m_antennaSeparation;       //the ratio of the distance between 2 antennas over wave length
  Ptr<UniformRandomVariable> m_uniformRv;
  Ptr<newradioPhyMacCommon> m_phyMacConfig;
  uint16_t m_startDistance;
  double m_speed;
};

} // namespace newradio

}  // namespace ns3


#endif /* newradio_CHANNEL_RAYTRACING_H_ */
