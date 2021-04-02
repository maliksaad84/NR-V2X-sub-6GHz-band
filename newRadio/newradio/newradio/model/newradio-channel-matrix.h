#ifndef newradio_CHANNEL_MATRIX_H_
#define newradio_CHANNEL_MATRIX_H_


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


struct ChannelParams : public SimpleRefCount<ChannelParams>
{
  complex2DVector_t       m_txSpatialMatrix;       // tx side spatial matrix
  complex2DVector_t       m_rxSpatialMatrix;       // rx side spatial matrix
  doubleVector_t          m_powerFraction;       // store subpath power fraction
  doubleVector_t          m_delaySpread;       // store delay spread
  doubleVector_t          m_doppler;       // store doppler



};

struct newradioBeamFormingParams : public SimpleRefCount<newradioBeamFormingParams>
{
  complexVector_t                 m_txW;       // tx antenna weights
  complexVector_t                 m_rxW;       // rx antenna weights
  Ptr<ChannelParams>      m_channelParams;
};


class newradioChannelMatrix : public SpectrumPropagationLossModel
{
public:
  newradioChannelMatrix ();
  virtual ~newradioChannelMatrix ();

  static TypeId GetTypeId (void);
  void DoDispose ();
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

  complex2DVector_t GenSpatialMatrix (std::vector<uint16_t> cluster, Angles angle, uint16_t* antennaNum) const;
  complexVector_t GenSinglePath (double hAngle, double vAngle, uint16_t* antennaNum) const;
  //complexVector_t CalcBeamformingVector (complex2DVector_t SpatialMatrix) const;
  Ptr<SpectrumValue> GetChannelGain (Ptr<const SpectrumValue> txPsd, Ptr<newradioBeamFormingParams> bfParams, double speed) const;
  double GetSystemBandwidth () const;

  mutable std::map< key_t, int > m_connectedPair;
  mutable std::map< key_t, Ptr<ChannelParams> > m_channelMatrixMap;
  double m_antennaSeparation;       //the ratio of the distance between 2 antennas over wave length
  double m_subBW;
  uint32_t m_numRB;
  uint32_t m_numSBPerRB;
  Ptr<UniformRandomVariable> m_uniformRv;
  Ptr<ExponentialRandomVariable> m_expRv;
  Ptr<newradioPhyMacCommon> m_phyMacConfig;

};

} // namespace newradio

}  // namespace ns3


#endif /* newradio_CHANNEL_MATRIX_H_ */
