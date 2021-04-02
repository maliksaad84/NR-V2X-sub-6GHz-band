#include "newradio-channel-raytracing.h"
#include <ns3/log.h>
#include <ns3/math.h>
#include <ns3/simulator.h>
#include <ns3/newradio-phy.h>
#include <ns3/newradio-net-device.h>
#include <ns3/node.h>
#include <ns3/newradio-ue-net-device.h>
#include <ns3/mc-ue-net-device.h>
#include <ns3/newradio-enb-net-device.h>
#include <ns3/antenna-array-model.h>
#include <ns3/newradio-ue-phy.h>
#include <ns3/newradio-enb-phy.h>
#include <ns3/double.h>
#include <algorithm>
#include <fstream>


namespace ns3 {

namespace newradio {

NS_LOG_COMPONENT_DEFINE ("newradioChannelRaytracing");

NS_OBJECT_ENSURE_REGISTERED (newradioChannelRaytracing);


doubleVector_t g_path; //number of multipath
double2DVector_t g_delay; //delay spread in ns
double2DVector_t g_pathloss;    //pathloss in DB
double2DVector_t g_phase;       //???what is this
double2DVector_t g_aodElevation;        //degree
double2DVector_t g_aodAzimuth;  //degree
double2DVector_t g_aoaElevation;        //degree
double2DVector_t g_aoaAzimuth;  //degree


newradioChannelRaytracing::newradioChannelRaytracing ()
  : m_antennaSeparation (0.5)
{
  m_uniformRv = CreateObject<UniformRandomVariable> ();
  LoadTraces ();

}

TypeId
newradioChannelRaytracing::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::newradioChannelRaytracing")
    .SetParent<Object> ()
    .AddAttribute ("StartDistance",
                   "Select start point of the simulation, the range of data point is [0, 260] meters",
                   UintegerValue (0),
                   MakeUintegerAccessor (&newradioChannelRaytracing::m_startDistance),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("Speed",
                   "The speed of the node (m/s)",
                   DoubleValue (1.0),
                   MakeDoubleAccessor (&newradioChannelRaytracing::m_speed),
                   MakeDoubleChecker<double> ())
  ;
  return tid;
}

newradioChannelRaytracing::~newradioChannelRaytracing ()
{

}

void
newradioChannelRaytracing::DoDispose ()
{
  NS_LOG_FUNCTION (this);
}

void
newradioChannelRaytracing::SetConfigurationParameters (Ptr<newradioPhyMacCommon> ptrConfig)
{
  m_phyMacConfig = ptrConfig;
}

Ptr<newradioPhyMacCommon>
newradioChannelRaytracing::GetConfigurationParameters (void) const
{
  return m_phyMacConfig;
}

void
newradioChannelRaytracing::LoadTraces ()
{
  std::string filename = "src/newradio/model/Raytracing/traces10cm.txt";
  NS_LOG_FUNCTION (this << "Loading Raytracing file " << filename);
  std::ifstream singlefile;
  singlefile.open (filename.c_str (), std::ifstream::in);

  NS_LOG_INFO (this << " File: " << filename);
  NS_ASSERT_MSG (singlefile.good (), " Raytracing file not found");
  std::string line;
  std::string token;
  uint16_t counter = 0;
  while ( std::getline (singlefile, line) )  //Parse each line of the file
    {
      if (counter == 8)
        {
          counter = 0;
        }
      doubleVector_t path;
      std::istringstream stream (line);
      while ( getline (stream,token,',') )  //Parse each comma separated string in a line
        {
          double sigma = 0.00;
          std::stringstream stream ( token );
          stream >> sigma;
          path.push_back (sigma);
        }

      switch (counter)
        {
        case 0:
          g_path.push_back (path.at (0));
          break;
        case 1:
          g_delay.push_back (path);
          break;
        case 2:
          g_pathloss.push_back (path);
          break;
        case 3:
          g_phase.push_back (path);
          break;
        case 4:
          g_aodElevation.push_back (path);
          break;
        case 5:
          g_aodAzimuth.push_back (path);
          break;
        case 6:
          g_aoaElevation.push_back (path);
          break;
        case 7:
          g_aoaAzimuth.push_back (path);
          break;
        default:
          NS_FATAL_ERROR ("Never call this");
          break;
        }
      counter++;
    }

  /*NS_LOG_UNCOND(g_path.size());
  NS_LOG_UNCOND(g_delay.size());
  NS_LOG_UNCOND(g_pathloss.size());
  NS_LOG_UNCOND(g_phase.size());
  NS_LOG_UNCOND(g_aodElevation.size());
  NS_LOG_UNCOND(g_aodAzimuth.size());
  NS_LOG_UNCOND(g_aoaElevation.size());
  NS_LOG_UNCOND(g_aoaAzimuth.size());*/



}


void
newradioChannelRaytracing::ConnectDevices (Ptr<NetDevice> dev1, Ptr<NetDevice> dev2)
{
  key_t key = std::make_pair (dev1,dev2);

  std::map< key_t, int >::iterator iter = m_connectedPair.find (key);
  if (iter != m_connectedPair.end ())
    {
      m_connectedPair.erase (iter);
    }
  m_connectedPair.insert (std::make_pair (key,1));
}

void
newradioChannelRaytracing::Initial (NetDeviceContainer ueDevices, NetDeviceContainer enbDevices)
{

  NS_LOG_INFO (&ueDevices << &enbDevices);

  for (NetDeviceContainer::Iterator i = ueDevices.Begin (); i != ueDevices.End (); i++)
    {
      for (NetDeviceContainer::Iterator j = enbDevices.Begin (); j != enbDevices.End (); j++)
        {
          SetBeamformingVector (*i,*j);
        }
    }

  for (NetDeviceContainer::Iterator i = ueDevices.Begin (); i != ueDevices.End (); i++)
    {
      Ptr<newradio::newradioUeNetDevice> UeDev =
        DynamicCast<newradio::newradioUeNetDevice> (*i);
      if (UeDev != 0)
        {
          if (UeDev->GetTargetEnb ())
            {
              Ptr<NetDevice> targetBs = UeDev->GetTargetEnb ();
              ConnectDevices (*i, targetBs);
              ConnectDevices (targetBs, *i);
            }
        }
      else
        {               // it may be a McUeNetDevice
          Ptr<McUeNetDevice> mcUeDev =
            DynamicCast<McUeNetDevice> (*i);
          if (mcUeDev != 0)
            {
              if (mcUeDev->GetnewradioTargetEnb ())
                {
                  Ptr<NetDevice> targetBs = mcUeDev->GetnewradioTargetEnb ();
                  ConnectDevices (*i, targetBs);
                  ConnectDevices (targetBs, *i);
                }
            }
        }
    }

}

Ptr<SpectrumValue>
newradioChannelRaytracing::DoCalcRxPowerSpectralDensity (Ptr<const SpectrumValue> txPsd,
                                                       Ptr<const MobilityModel> a,
                                                       Ptr<const MobilityModel> b) const
{
  NS_LOG_FUNCTION (this);
  Ptr<SpectrumValue> rxPsd = Copy (txPsd);
  Ptr<AntennaArrayModel> txAntennaArray, rxAntennaArray;

  Ptr<NetDevice> txDevice = a->GetObject<Node> ()->GetDevice (0);
  Ptr<NetDevice> rxDevice = b->GetObject<Node> ()->GetDevice (0);
  Ptr<newradioEnbNetDevice> txEnb =
    DynamicCast<newradioEnbNetDevice> (txDevice);
  Ptr<newradio::newradioUeNetDevice> rxUe =
    DynamicCast<newradio::newradioUeNetDevice> (rxDevice);

  uint16_t txAntennaNum[2];
  uint16_t rxAntennaNum[2];

  bool dl = true;

  if (txEnb != 0 && rxUe != 0)
    {
      NS_LOG_INFO ("this is downlink case");

      txAntennaNum[0] = sqrt (txEnb->GetAntennaNum ());
      txAntennaNum[1] = sqrt (txEnb->GetAntennaNum ());
      rxAntennaNum[0] = sqrt (rxUe->GetAntennaNum ());
      rxAntennaNum[1] = sqrt (rxUe->GetAntennaNum ());

      txAntennaArray = DynamicCast<AntennaArrayModel> (
          txEnb->GetPhy ()->GetDlSpectrumPhy ()->GetRxAntenna ());
      rxAntennaArray = DynamicCast<AntennaArrayModel> (
          rxUe->GetPhy ()->GetDlSpectrumPhy ()->GetRxAntenna ());
    }
  else if (txEnb != 0 && rxUe == 0)
    {
      Ptr<McUeNetDevice> rxMcUe = DynamicCast<McUeNetDevice> (rxDevice);

      if (rxMcUe != 0)
        {
          NS_LOG_INFO ("this is downlink case for MC device");

          txAntennaNum[0] = sqrt (txEnb->GetAntennaNum ());
          txAntennaNum[1] = sqrt (txEnb->GetAntennaNum ());
          rxAntennaNum[0] = sqrt (rxMcUe->GetAntennaNum ());
          rxAntennaNum[1] = sqrt (rxMcUe->GetAntennaNum ());

          txAntennaArray = DynamicCast<AntennaArrayModel> (
              txEnb->GetPhy ()->GetDlSpectrumPhy ()->GetRxAntenna ());
          rxAntennaArray = DynamicCast<AntennaArrayModel> (
              rxMcUe->GetnewradioPhy ()->GetDlSpectrumPhy ()->GetRxAntenna ());
        }
    }
  else if (txEnb == 0 && rxUe == 0 )
    {
      NS_LOG_INFO ("this is uplink case");

      dl = false;
      Ptr<newradio::newradioUeNetDevice> txUe =
        DynamicCast<newradio::newradioUeNetDevice> (txDevice);
      Ptr<newradioEnbNetDevice> rxEnb =
        DynamicCast<newradioEnbNetDevice> (rxDevice);

      if (txUe != 0)
        {
          NS_LOG_INFO ("this is uplink case");
          txAntennaNum[0] = sqrt (txUe->GetAntennaNum ());
          txAntennaNum[1] = sqrt (txUe->GetAntennaNum ());
          rxAntennaNum[0] = sqrt (rxEnb->GetAntennaNum ());
          rxAntennaNum[1] = sqrt (rxEnb->GetAntennaNum ());

          txAntennaArray = DynamicCast<AntennaArrayModel> (
              txUe->GetPhy ()->GetDlSpectrumPhy ()->GetRxAntenna ());
          rxAntennaArray = DynamicCast<AntennaArrayModel> (
              rxEnb->GetPhy ()->GetDlSpectrumPhy ()->GetRxAntenna ());
        }
      else
        {
          Ptr<McUeNetDevice> txMcUe = DynamicCast<McUeNetDevice> (txDevice);

          if (txMcUe != 0)
            {
              NS_LOG_INFO ("this is uplink case for MC device");

              txAntennaNum[0] = sqrt (txMcUe->GetAntennaNum ());
              txAntennaNum[1] = sqrt (txMcUe->GetAntennaNum ());
              rxAntennaNum[0] = sqrt (rxEnb->GetAntennaNum ());
              rxAntennaNum[1] = sqrt (rxEnb->GetAntennaNum ());

              txAntennaArray = DynamicCast<AntennaArrayModel> (
                  txMcUe->GetnewradioPhy ()->GetDlSpectrumPhy ()->GetRxAntenna ());
              rxAntennaArray = DynamicCast<AntennaArrayModel> (
                  rxEnb->GetPhy ()->GetDlSpectrumPhy ()->GetRxAntenna ());

            }
        }
    }
  else
    {
      NS_LOG_INFO ("enb to enb or ue to ue transmission, skip beamforming");
      return rxPsd;
    }

  Ptr<newradioBeamFormingTraces> bfParams = Create<newradioBeamFormingTraces> ();
  key_t key = std::make_pair (txDevice,rxDevice);

  double time = Simulator::Now ().GetSeconds ();
  uint16_t traceIndex = (m_startDistance + time * m_speed) * 100;
  static uint16_t currentIndex = m_startDistance * 100;
  if (traceIndex > 26050)
    {
      NS_FATAL_ERROR ("The maximum trace index is 26050");
    }
  if (traceIndex != currentIndex)
    {
      currentIndex = traceIndex;
      m_channelMatrixMap.clear ();
    }

  std::map< key_t, Ptr<TraceParams> >::iterator it = m_channelMatrixMap.find (key);
  if (it == m_channelMatrixMap.end ())
    {

      complex2DVector_t txSpatialMatrix;
      complex2DVector_t rxSpatialMatrix;
      if (dl)
        {
          txSpatialMatrix = GenSpatialMatrix (traceIndex,txAntennaNum, true);
          rxSpatialMatrix = GenSpatialMatrix (traceIndex,rxAntennaNum, false);
        }
      else
        {
          txSpatialMatrix = GenSpatialMatrix (traceIndex,txAntennaNum, false);
          rxSpatialMatrix = GenSpatialMatrix (traceIndex,rxAntennaNum, true);
        }
      doubleVector_t dopplerShift;
      for (unsigned int i = 0; i < g_path.at (traceIndex); i++)
        {
          dopplerShift.push_back (m_uniformRv->GetValue (0,1));
        }

      Ptr<TraceParams> channel = Create<TraceParams> ();

      channel->m_txSpatialMatrix = txSpatialMatrix;
      channel->m_rxSpatialMatrix = rxSpatialMatrix;
      channel->m_powerFraction = g_pathloss.at (traceIndex);
      channel->m_delaySpread = g_delay.at (traceIndex);
      channel->m_doppler = dopplerShift;


      m_channelMatrixMap.insert (std::make_pair (key,channel));

      key_t reverseKey = std::make_pair (rxDevice,txDevice);
      Ptr<TraceParams> reverseChannel = Create<TraceParams> ();
      reverseChannel->m_txSpatialMatrix = rxSpatialMatrix;
      reverseChannel->m_rxSpatialMatrix = txSpatialMatrix;
      reverseChannel->m_powerFraction = g_pathloss.at (traceIndex);
      reverseChannel->m_delaySpread = g_delay.at (traceIndex);
      reverseChannel->m_doppler = dopplerShift;

      m_channelMatrixMap.insert (std::make_pair (reverseKey,reverseChannel));

      bfParams->m_channelParams = channel;
    }
  else
    {
      bfParams->m_channelParams = (*it).second;
    }

  //	calculate antenna weights, better method should be implemented
  bfParams->m_txW = txAntennaArray->GetBeamformingVectorPanel ();
  NS_LOG_LOGIC ("TX size " << bfParams->m_txW.size ());
  bfParams->m_rxW = rxAntennaArray->GetBeamformingVectorPanel ();
  NS_LOG_LOGIC ("RX size " << bfParams->m_rxW.size ());

  std::map< key_t, int >::iterator it1 = m_connectedPair.find (key);
  if (it1 != m_connectedPair.end ())
    {
      bfParams->m_txW = CalcBeamformingVector (bfParams->m_channelParams->m_txSpatialMatrix, bfParams->m_channelParams->m_powerFraction);
      bfParams->m_rxW = CalcBeamformingVector (bfParams->m_channelParams->m_rxSpatialMatrix, bfParams->m_channelParams->m_powerFraction);
      txAntennaArray->SetBeamformingVectorPanel (bfParams->m_txW,rxDevice);
      txAntennaArray->ChangeBeamformingVectorPanel (rxDevice);
      rxAntennaArray->SetBeamformingVectorPanel (bfParams->m_rxW,txDevice);
      rxAntennaArray->ChangeBeamformingVectorPanel (txDevice);
    }

  /*Vector rxSpeed = b->GetVelocity();
  Vector txSpeed = a->GetVelocity();
  double relativeSpeed = (rxSpeed.x-txSpeed.x)
                  +(rxSpeed.y-txSpeed.y)+(rxSpeed.z-txSpeed.z);*/
  Ptr<SpectrumValue> bfPsd = GetChannelGain (rxPsd,bfParams, m_speed);


  /*loging the beamforming gain for debug*/
  /*Values::iterator bfit = bfPsd->ValuesBegin ();
  Values::iterator rxit = rxPsd->ValuesBegin ();
  uint32_t subChannel = 0;
  double value=0;
  while (bfit != bfPsd->ValuesEnd ())
  {
          value += (*bfit)/(*rxit);
          bfit++;
          rxit++;
          subChannel++;
  }*/
  //NS_LOG_UNCOND("Gain("<<10*log10(value/72)<<"dB)");

  //NS_LOG_UNCOND ("TxAngle("<<txAngles.phi*180/M_PI<<") RxAngle("<<rxAngles.phi*180/M_PI
  //		<<") Speed["<<relativeSpeed<<"]");
  //NS_LOG_UNCOND ("Gain("<<10*Log10((*bfPsd)/(*txPsd))<<"dB)");
  return bfPsd;


}

void
newradioChannelRaytracing::SetBeamformingVector (Ptr<NetDevice> ueDevice, Ptr<NetDevice> enbDevice)
{
  Ptr<newradioEnbNetDevice> EnbDev =
    DynamicCast<newradioEnbNetDevice> (enbDevice);
  Ptr<newradio::newradioUeNetDevice> UeDev =
    DynamicCast<newradio::newradioUeNetDevice> (ueDevice);

  uint8_t ccId = m_phyMacConfig->GetCcId ();
  if (UeDev != 0)
    {
      NS_LOG_LOGIC ("SetBeamformingVector between UE " << ueDevice << " and enbDevice " << enbDevice);
      Ptr<AntennaArrayModel> ueAntennaArray = DynamicCast<AntennaArrayModel> (
          UeDev->GetPhy (ccId)->GetDlSpectrumPhy ()->GetRxAntenna ());
      Ptr<AntennaArrayModel> enbAntennaArray = DynamicCast<AntennaArrayModel> (
          EnbDev->GetPhy (ccId)->GetDlSpectrumPhy ()->GetRxAntenna ());
      complexVector_t dummy;
      ueAntennaArray->SetBeamformingVectorPanel (dummy,enbDevice);
      enbAntennaArray->SetBeamformingVectorPanel (dummy,ueDevice);
      ueAntennaArray->SetBeamformingVectorPanelDevices (ueDevice,enbDevice);
      enbAntennaArray->SetBeamformingVectorPanelDevices (enbDevice,ueDevice);
    }
  else
    {
      Ptr<McUeNetDevice> UeDev =
        DynamicCast<McUeNetDevice> (ueDevice);
      if (UeDev != 0)
        {
          NS_LOG_LOGIC ("SetBeamformingVector between UE " << ueDevice << " and enbDevice " << enbDevice);
          Ptr<AntennaArrayModel> ueAntennaArray = DynamicCast<AntennaArrayModel> (
              UeDev->GetnewradioPhy (ccId)->GetDlSpectrumPhy ()->GetRxAntenna ());
          Ptr<AntennaArrayModel> enbAntennaArray = DynamicCast<AntennaArrayModel> (
              EnbDev->GetPhy (ccId)->GetDlSpectrumPhy ()->GetRxAntenna ());
          complexVector_t dummy;
          ueAntennaArray->SetBeamformingVectorPanel (dummy,enbDevice);
          enbAntennaArray->SetBeamformingVectorPanel (dummy,ueDevice);
          ueAntennaArray->SetBeamformingVectorPanelDevices (ueDevice,enbDevice);
          enbAntennaArray->SetBeamformingVectorPanelDevices (enbDevice,ueDevice);
        }
      else
        {
          NS_FATAL_ERROR ("Unrecognized pair of devices");
        }
    }



}


complexVector_t
newradioChannelRaytracing::CalcBeamformingVector (complex2DVector_t spatialMatrix, doubleVector_t powerFraction) const
{
  NS_LOG_LOGIC ("CalcBeamformingVector for " << spatialMatrix.at (0).size ());
  complexVector_t antennaWeights;
  uint16_t antennaNum = spatialMatrix.at (0).size ();
  for (int i = 0; i < antennaNum; i++)
    {
      antennaWeights.push_back (spatialMatrix.at (0).at (i) / sqrt (antennaNum));
    }

  for (int iter = 0; iter < 10; iter++)
    {
      complexVector_t antennaWeights_New;

      for (unsigned pathIndex = 0; pathIndex < spatialMatrix.size (); pathIndex++)
        {
          std::complex<double> sum;
          for (int i = 0; i < antennaNum; i++)
            {
              sum += std::conj (spatialMatrix.at (pathIndex).at (i)) * antennaWeights.at (i);
            }

          for (int i = 0; i < antennaNum; i++)
            {
              double pathPowerLinear = std::pow (10.0, (powerFraction.at (pathIndex)) / 10.0);


              if (pathIndex == 0)
                {
                  antennaWeights_New.push_back (pathPowerLinear * spatialMatrix.at (pathIndex).at (i) * sum);
                }
              else
                {
                  antennaWeights_New.at (i) += pathPowerLinear * spatialMatrix.at (pathIndex).at (i) * sum;
                }
            }
        }
      //normalize antennaWeights;
      double weightSum = 0;
      for (int i = 0; i < antennaNum; i++)
        {
          weightSum += pow (std::abs (antennaWeights_New.at (i)),2);
        }
      for (int i = 0; i < antennaNum; i++)
        {
          antennaWeights_New.at (i) = antennaWeights_New.at (i) / sqrt (weightSum);
        }
      antennaWeights = antennaWeights_New;
    }
  return antennaWeights;
}



complex2DVector_t
newradioChannelRaytracing::GenSpatialMatrix (uint64_t traceIndex, uint16_t* antennaNum, bool bs) const
{
  complex2DVector_t spatialMatrix;
  uint16_t pathNum = g_path.at (traceIndex);
  for (unsigned int pathIndex = 0; pathIndex < pathNum; pathIndex++)
    {
      double azimuthAngle;
      double verticalAngle;
      if (bs)
        {
          azimuthAngle = g_aodAzimuth.at (traceIndex).at (pathIndex);
          verticalAngle = g_aodElevation.at (traceIndex).at (pathIndex);
        }
      else
        {
          azimuthAngle = g_aoaAzimuth.at (traceIndex).at (pathIndex);
          verticalAngle = g_aoaElevation.at (traceIndex).at (pathIndex);
        }
      complexVector_t singlePath;
      singlePath = GenSinglePath (azimuthAngle * M_PI / 180, verticalAngle * M_PI / 180, antennaNum);
      spatialMatrix.push_back (singlePath);
    }

  return spatialMatrix;
}


complexVector_t
newradioChannelRaytracing::GenSinglePath (double hAngle, double vAngle, uint16_t* antennaNum) const
{
  NS_LOG_FUNCTION (this);
  complexVector_t singlePath;
  uint64_t vSize = antennaNum[0];
  uint64_t hSize = antennaNum[1];

  for (uint64_t vIndex = 0; vIndex < vSize; vIndex++)
    {
      for (uint64_t hIndex = 0; hIndex < hSize; hIndex++)
        {
          double w = (-2) * M_PI * hIndex * m_antennaSeparation * cos (hAngle)
            + (-2) * M_PI * vIndex * m_antennaSeparation * cos (vAngle);
          singlePath.push_back (std::complex<double> (cos (w), sin (w)));
        }
    }
  return singlePath;
}

Ptr<SpectrumValue>
newradioChannelRaytracing::GetChannelGain (Ptr<const SpectrumValue> txPsd, Ptr<newradioBeamFormingTraces> bfParams, double speed) const
{
  NS_LOG_FUNCTION (this);

  NS_ASSERT (bfParams->m_channelParams->m_rxSpatialMatrix.size () == bfParams->m_channelParams->m_txSpatialMatrix.size ());
  uint16_t pathNum = bfParams->m_channelParams->m_txSpatialMatrix.size ();
  Time time = Simulator::Now ();
  double t = time.GetSeconds ();
  Ptr<SpectrumValue> tempPsd = Copy<SpectrumValue> (txPsd);
  bool noSpeed = false;
  if (speed == 0)
    {
      noSpeed = true;
    }

  Values::iterator vit = tempPsd->ValuesBegin ();
  uint16_t iSubband = 0;
  while (vit != tempPsd->ValuesEnd ())
    {
      std::complex<double> subsbandGain (0.0,0.0);
      if ((*vit) != 0.00)
        {
          double fsb = m_phyMacConfig->GetCenterFrequency () - GetSystemBandwidth () / 2 + m_phyMacConfig->GetChunkWidth () * iSubband;
          for (unsigned int pathIndex = 0; pathIndex < pathNum; pathIndex++)
            {
              //need to convert ns to s
              double temp_delay = -1e-9 * 2*M_PI*fsb*bfParams->m_channelParams->m_delaySpread.at (pathIndex);
              std::complex<double> delay (cos (temp_delay), sin (temp_delay));

              std::complex<double> doppler;
              if (noSpeed)
                {
                  doppler = std::complex<double> (1,0);
                }
              else
                {
                  double f_d = speed * m_phyMacConfig->GetCenterFrequency () / 3e8;
                  double temp_Doppler = 2*M_PI*t*f_d*bfParams->m_channelParams->m_doppler.at (pathIndex);
                  doppler = std::complex<double> (cos (temp_Doppler), sin (temp_Doppler));
                }
              double pathPowerLinear = std::pow (10.0, (bfParams->m_channelParams->m_powerFraction.at (pathIndex)) / 10.0);

              std::complex<double> smallScaleFading = sqrt (pathPowerLinear) * doppler * delay;
              NS_LOG_INFO (doppler << delay);

              if (bfParams->m_txW.empty ()||bfParams->m_rxW.empty ())
                {
                  NS_FATAL_ERROR ("antenna weights are empty");
                }
              else
                {
                  /* beam forming*/
                  std::complex<double> txSum, rxSum;
                  for (unsigned i = 0; i < bfParams->m_txW.size (); i++)
                    {
                      txSum += std::conj (bfParams->m_channelParams->m_txSpatialMatrix.at (pathIndex).at (i)) * bfParams->m_txW.at (i);
                    }
                  for (unsigned i = 0; i < bfParams->m_rxW.size (); i++)
                    {
                      rxSum += bfParams->m_channelParams->m_rxSpatialMatrix.at (pathIndex).at (i) * std::conj (bfParams->m_rxW.at (i));
                    }
                  subsbandGain = subsbandGain + txSum * rxSum * smallScaleFading;
                }
            }
          *vit = (*vit) * (norm (subsbandGain));
        }
      vit++;
      iSubband++;
    }
  return tempPsd;
}

double
newradioChannelRaytracing::GetSystemBandwidth () const
{
  double bw = 0.00;
  bw = m_phyMacConfig->GetChunkWidth () * m_phyMacConfig->GetNumChunkPerRb () * m_phyMacConfig->GetNumRb ();
  return bw;
}


Ptr<SpectrumValue>
newradioChannelRaytracing::CalcRxPowerSpectralDensity (Ptr<const SpectrumValue> txPsd,
                                                     Ptr<const MobilityModel> a,
                                                     Ptr<const MobilityModel> b) const
{
  return DoCalcRxPowerSpectralDensity (txPsd, a, b);
}

} // namespace newradio

} // namespace ns3
