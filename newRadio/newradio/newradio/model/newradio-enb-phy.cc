#include <ns3/object-factory.h>
#include <ns3/log.h>
#include <cfloat>
#include <cmath>
#include <ns3/simulator.h>
#include <ns3/attribute-accessor-helper.h>
#include <ns3/double.h>

#include "newradio-enb-phy.h"
#include "newradio-ue-phy.h"
#include "newradio-net-device.h"
#include "newradio-ue-net-device.h"
#include "newradio-spectrum-value-helper.h"
#include "newradio-radio-bearer-tag.h"
#include "mc-ue-net-device.h"

#include "newradio-beamforming.h"
#include "newradio-channel-matrix.h"
#include "newradio-channel-raytracing.h"
#include "newradio-3gpp-channel.h"

#include <ns3/node-list.h>
#include <ns3/node.h>
#include <ns3/pointer.h>
#include <math.h>
#include <ns3/random-variable-stream.h>
#include <complex.h>

#include <iostream>
#include <fstream>
#include <ns3/average.h>
#include <algorithm>
#include <array>


namespace ns3 {

namespace newradio {

NS_LOG_COMPONENT_DEFINE ("newradioEnbPhy");

NS_OBJECT_ENSURE_REGISTERED (newradioEnbPhy);

newradioEnbPhy::newradioEnbPhy ()
{
  NS_LOG_FUNCTION (this);
  NS_FATAL_ERROR ("This constructor should not be called");
}

newradioEnbPhy::newradioEnbPhy (Ptr<newradioSpectrumPhy> dlPhy, Ptr<newradioSpectrumPhy> ulPhy)
  : newradioPhy (dlPhy, ulPhy),
    m_prevSlot (0),
    m_prevSlotDir (SlotAllocInfo::NA),
    m_currSymStart (0)
{
  m_enbCphySapProvider = new MemberLteEnbCphySapProvider<newradioEnbPhy> (this);
  m_roundFromLastUeSinrUpdate = 0;
  Simulator::ScheduleNow (&newradioEnbPhy::StartSubFrame, this);
}

newradioEnbPhy::~newradioEnbPhy ()
{

}

TypeId
newradioEnbPhy::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::newradioEnbPhy")
    .SetParent<newradioPhy> ()
    .AddConstructor<newradioEnbPhy> ()
    .AddAttribute ("TxPower",
                   "Transmission power in dBm",
                   DoubleValue (30.0),
                   MakeDoubleAccessor (&newradioEnbPhy::SetTxPower,
                                       &newradioEnbPhy::GetTxPower),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("NoiseAndFilter",
                   "If true, use noisy SINR samples, filtered. If false, just use the SINR measure",
                   BooleanValue (false),
                   MakeBooleanAccessor (&newradioEnbPhy::m_noiseAndFilter),
                   MakeBooleanChecker ())
    .AddAttribute ("UpdateSinrEstimatePeriod",
                   "Period (in microseconds) of update of SINR estimate of all the UE",
                   IntegerValue (1600),     //TODO considering refactoring in newradioPhyMacCommon
                   MakeIntegerAccessor (&newradioEnbPhy::m_updateSinrPeriod),
                   MakeIntegerChecker<int> ())
    .AddAttribute ("UpdateUeSinrEstimatePeriod",
                   "Period (in ms) of reporting of SINR estimate of all the UE",
                   DoubleValue (25.6),
                   MakeDoubleAccessor (&newradioEnbPhy::m_ueUpdateSinrPeriod),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("Transient",
                   "Transient period (in microseconds) in which just collect SINR values without filtering the sample",
                   IntegerValue (320000),
                   MakeIntegerAccessor (&newradioEnbPhy::m_transient),
                   MakeIntegerChecker<int> ())
    .AddAttribute ("NoiseFigure",
                   "Loss (dB) in the Signal-to-Noise-Ratio due to non-idealities in the receiver."
                   " According to Wikipedia (http://en.wikipedia.org/wiki/Noise_figure), this is "
                   "\"the difference in decibels (dB) between"
                   " the noise output of the actual receiver to the noise output of an "
                   " ideal receiver with the same overall gain and bandwidth when the receivers "
                   " are connected to sources at the standard noise temperature T0.\" "
                   "In this model, we consider T0 = 290K.",
                   DoubleValue (5.0),
                   MakeDoubleAccessor (&newradioPhy::SetNoiseFigure,
                                       &newradioPhy::GetNoiseFigure),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("DlSpectrumPhy",
                   "The downlink newradioSpectrumPhy associated to this newradioPhy",
                   TypeId::ATTR_GET,
                   PointerValue (),
                   MakePointerAccessor (&newradioEnbPhy::GetDlSpectrumPhy),
                   MakePointerChecker <newradioSpectrumPhy> ())
    .AddAttribute ("UlSpectrumPhy",
                   "The uplink newradioSpectrumPhy associated to this newradioPhy",
                   TypeId::ATTR_GET,
                   PointerValue (),
                   MakePointerAccessor (&newradioEnbPhy::GetUlSpectrumPhy),
                   MakePointerChecker <newradioSpectrumPhy> ())
    .AddTraceSource ("UlSinrTrace",
                     "UL SINR statistics.",
                     MakeTraceSourceAccessor (&newradioEnbPhy::m_ulSinrTrace),
                     "ns3::UlSinr::TracedCallback")

  ;
  return tid;

}

void
newradioEnbPhy::DoInitialize (void)
{
  NS_LOG_FUNCTION (this);
  Ptr<SpectrumValue> noisePsd = newradioSpectrumValueHelper::CreateNoisePowerSpectralDensity (m_phyMacConfig, m_noiseFigure);
  m_downlinkSpectrumPhy->SetNoisePowerSpectralDensity (noisePsd);
  //m_numRbg = m_phyMacConfig->GetNumRb() / m_phyMacConfig->GetNumRbPerRbg();
  //m_ctrlPeriod = NanoSeconds (1000 * m_phyMacConfig->GetCtrlSymbols() * m_phyMacConfig->GetSymbolPeriod());
  //m_dataPeriod = NanoSeconds (1000 * (m_phyMacConfig->GetSymbPerSlot() - m_phyMacConfig->GetCtrlSymbols()) * m_phyMacConfig->GetSymbolPeriod());

  for (unsigned i = 0; i < m_phyMacConfig->GetL1L2CtrlLatency (); i++)
    {     // push elements onto queue for initial scheduling delay
      m_controlMessageQueue.push_back (std::list<Ptr<newradioControlMessage> > ());
    }
  //m_sfAllocInfoUpdated = true;

  for (unsigned i = 0; i < m_phyMacConfig->GetTotalNumChunk (); i++)
    {
      m_channelChunks.push_back (i);
    }
  SetSubChannels (m_channelChunks);

  m_sfPeriod = NanoSeconds (1000.0 * m_phyMacConfig->GetSubframePeriod ());

  for (unsigned i = 0; i < m_phyMacConfig->GetSubframesPerFrame (); i++)
    {
      m_sfAllocInfo.push_back (SfAllocInfo (SfnSf (m_frameNum, i, 0)));
      SlotAllocInfo dlCtrlSlot;
      dlCtrlSlot.m_slotType = SlotAllocInfo::CTRL;
      dlCtrlSlot.m_numCtrlSym = 1;
      dlCtrlSlot.m_tddMode = SlotAllocInfo::DL_slotAllocInfo;
      dlCtrlSlot.m_dci.m_numSym = 1;
      dlCtrlSlot.m_dci.m_symStart = 0;
      SlotAllocInfo ulCtrlSlot;
      ulCtrlSlot.m_slotType = SlotAllocInfo::CTRL;
      ulCtrlSlot.m_numCtrlSym = 1;
      ulCtrlSlot.m_tddMode = SlotAllocInfo::UL_slotAllocInfo;
      ulCtrlSlot.m_slotIdx = 0xFF;
      ulCtrlSlot.m_dci.m_numSym = 1;
      ulCtrlSlot.m_dci.m_symStart = m_phyMacConfig->GetSymbolsPerSubframe () - 1;
      m_sfAllocInfo[i].m_slotAllocInfo.push_back (dlCtrlSlot);
      m_sfAllocInfo[i].m_slotAllocInfo.push_back (ulCtrlSlot);
    }

  NS_LOG_DEBUG ("In newradioEnbPhy, the RT periodicity is: " << m_updateSinrPeriod << " microseconds");
  NS_LOG_DEBUG ("In newradioEnbPhy, the transient duration is: " << m_transient << " microseconds");
  if (m_noiseAndFilter)
    {
      NS_ASSERT_MSG ((double)m_transient / m_updateSinrPeriod >= 16, "Window too small to compute the variance according to the ApplyFilter method");
    }
  Simulator::Schedule (MicroSeconds (0), &newradioEnbPhy::UpdateUeSinrEstimate, this);
  Simulator::Schedule (MicroSeconds (0), &newradioEnbPhy::CallPathloss, this);
  newradioPhy::DoInitialize ();
}
void
newradioEnbPhy::DoDispose (void)
{

}



//Function for average
double
newradioEnbPhy::MakeAvg ( std::vector<double> v )
{
  double return_value = 0.0;
  int n = v.size ();

  for ( int i = 0; i < n; i++)
    {
      return_value += v.at (i);
    }

  return ( return_value / v.size ());
}
//****************End of average funtion****************


//Function for variance
double
newradioEnbPhy::MakeVar ( std::vector<double> v, double mean )
{
  double sum = 0.0;
  double temp = 0.0;
  double var = 0.0;
  int n = v.size ();

  for ( int j = 0; j < n; j++)
    {
      temp = std::pow ((v.at (j) - mean),2);
      sum += temp;
    }

  return var = sum / (v.size ());
}
//****************End of variance funtion****************

double
newradioEnbPhy::AddGaussianNoise (double LastSinrValue)
{
  double N0 = 3.98107170e-12;
  std::complex<double> gaussianNoise;
  double signalEnergy;
  double noisySample;

  Ptr<NormalRandomVariable> randomVariable = CreateObject<NormalRandomVariable> ();
  double gaussianSampleRe = randomVariable->GetValue ();
  double gaussianSampleIm = randomVariable->GetValue ();
  gaussianNoise = std::complex<double> (sqrt (0.5) * sqrt (N0) * gaussianSampleRe, sqrt (0.5) * sqrt (N0) * gaussianSampleIm);

  signalEnergy = LastSinrValue * N0;

  noisySample = (std::pow (std::abs (sqrt (signalEnergy) + gaussianNoise),2) - N0) / N0;

  return noisySample;
}



std::pair<uint64_t, uint64_t>
newradioEnbPhy::ApplyFilter (std::vector<double> noisySinr)
{
  std::vector<double> noisySinrdB;
  for (uint64_t i = 0; i < noisySinr.size (); ++i)
    {
      noisySinrdB.push_back (10 * std::log10 (noisySinr.at (i)));
    }

  std::vector<double> vectorVar;
  NS_LOG_DEBUG ("noisySinrdBSize() " << noisySinrdB.size ());
  for (uint64_t i = 0; i < noisySinrdB.size () - 1; ++i)
    {
      std::vector<double> partialSamples;
      partialSamples.push_back (noisySinrdB.at (i));
      partialSamples.push_back (noisySinrdB.at (i + 1));
      double meanValue = MakeAvg (partialSamples);
      double varValue = MakeVar (partialSamples,meanValue);
      vectorVar.push_back (varValue);
      partialSamples.clear ();
    }

  uint64_t startFilter = 1e6;
  uint64_t endFilter = 1e6;

  bool flagStartFilter = true;
  bool flagEndFilter = true;

  uint64_t noisySinrIndex = 0;

  for (uint64_t varIndex = vectorVar.size () - 1; varIndex > 0; varIndex--)      // start filter when variance of the noisy trace is high
    {
      NS_LOG_DEBUG ("varIndex " << varIndex);
      noisySinrIndex = varIndex + 1;
      NS_LOG_DEBUG ("vectorVar[i] " << vectorVar.at (varIndex));
      NS_LOG_DEBUG ("vectorVar[i] == NaN " << (std::isnan (vectorVar.at (varIndex))));
      bool highVariance = (vectorVar.at (varIndex) > 5 || std::isnan (vectorVar.at (varIndex)));
      bool lowSinr = noisySinr.at (noisySinrIndex) < 10;

      if (highVariance || (lowSinr && !highVariance))            // filter is applied only for low-SINR regimes [dB]
        {
          endFilter = noisySinrIndex;
          flagEndFilter = false;               // a "start" sample has been identified
          break;
        }
    }

  if (flagEndFilter)       // in this case, we can avoid the filtering
    {
      endFilter = 0;
    }

  /* in this case, consider at least a window of 15 samples, after which we can consider
  * as we are leaving the blockage phase and we start coming back to LOS PL regimes
  */
  uint64_t varIndex = 0;
  uint64_t numberOfVarWindow = 16;
  NS_LOG_DEBUG ("VectorVarSize() " << vectorVar.size ());
  for (uint64_t noisySinrIndex = endFilter; noisySinrIndex > numberOfVarWindow; --noisySinrIndex)       // must be at least after the beginnning of the blocakge
    {
      NS_LOG_DEBUG ("noisySinrIndex " << noisySinrIndex);
      varIndex = noisySinrIndex - 1;

      std::vector<double>::const_iterator first = vectorVar.begin () + varIndex;
      std::vector<double>::const_iterator last = vectorVar.begin () + varIndex - (numberOfVarWindow - 1);          // vectorVar has one sample less than noisySinrdB
      std::vector<double> prov (last,first);

      std::vector<double>::const_iterator firstNoisy = noisySinrdB.begin () + noisySinrIndex;
      std::vector<double>::const_iterator lastNoisy = noisySinrdB.begin () + noisySinrIndex - numberOfVarWindow;
      std::vector<double> provNoisy (lastNoisy,firstNoisy);

      NS_LOG_INFO ("provNoisy.size " << provNoisy.size ());
      for (std::vector<double>::const_iterator h = provNoisy.begin (); h != provNoisy.end (); h++)
        {
          NS_LOG_INFO ("h " << *h);
          NS_LOG_INFO ("i " << noisySinrIndex  );
          NS_LOG_INFO ("hh " << *(noisySinrdB.begin () + noisySinrIndex - numberOfVarWindow));
        }


      /* the filtering ends when the variance of the noisy trace is almost the same, so when
      * the SINR is on sufficiently high values
      */

      if (Simulator::Now () > Seconds (2.1) && Simulator::Now () < Seconds (2.3))
        {
          NS_LOG_DEBUG ("(std::all_of(prov.begin(),prov.end(), [](double j){return j < 1;})) " << (std::all_of (prov.begin (),prov.end (), [] (double j){
                                                                                                                  return j < 1;
                                                                                                                })));
          NS_LOG_DEBUG ("(std::all_of(prov.begin(),prov.end(), [](double j){nan;})) " << (std::all_of (prov.begin (),prov.end (), [] (double k){
                                                                                                         return !std::isnan (k);
                                                                                                       })));
          NS_LOG_DEBUG ("(std::all_of(provNoisy.begin(),provNoisy.end(), [](double p){return p > 10;})) " << (std::all_of (provNoisy.begin (),provNoisy.end (), [] (double p){
                                                                                                                             return p > 10;
                                                                                                                           })));
        }

      if (((std::all_of (prov.begin (),prov.end (), [] (double j){
                           return j < 1;
                         }))
           && (std::all_of (prov.begin (),prov.end (), [] (double k){
                              return !std::isnan (k);
                            })))
          || (std::all_of (provNoisy.begin (),provNoisy.end (), [] (double p){
                             return p > 10;
                           })))
        {
          startFilter = noisySinrIndex;
          flagStartFilter = false;               // a "end" sample has been identified
          break;
        }

      // bool lowVariance = (vectorVar.at(varIndex) < 1 && !std::isnan(vectorVar.at(varIndex)));
      // bool highSinr = noisySinr.at(noisySinrIndex) > 10;

      // if (highSinr && lowVariance)  // filter is applied only for low-SINR regimes [dB]
      // {
      //        startFilter = noisySinrIndex;
      //        flagStartFilter = false; // a "start" sample has been identified
      //        break;
      // }

    }

  if (flagStartFilter)       // in this case, filter till the end of the trace
    {
      startFilter = 0;
    }


  std::pair< uint64_t, uint64_t > pairFiltering = std::make_pair (startFilter,endFilter);

  return pairFiltering;
}

std::vector<double>
newradioEnbPhy::MakeFilter (std::vector<double> noisySinr, std::vector<double> realSinr, std::pair <uint64_t, uint64_t > pairFiltering)
{

  for (uint64_t i = 0; i < noisySinr.size (); ++i)
    {
      NS_LOG_DEBUG ("() " << noisySinr.at (i));
    }
  //const uint64_t lengthFiltering  = (std::get<1>(pairFiltering) - std::get<0>(pairFiltering));
  /* find best alpha parameter for the Kalman estimation */
  int rep = 0;
  std::array<double,100> meanError;
  for (double alpha = 0; alpha < 1; alpha = alpha + 0.01 )
    {

      std::vector<double>  x;
      x.push_back (0);           // initialization of array
      int counter = 0;

      for (uint64_t i = std::get<0> (pairFiltering); i < std::get<1> (pairFiltering); i++)
        {
          x.push_back ((1 - alpha) * x.at (counter) + alpha * (noisySinr.at (i)));
          counter++;
        }

      std::vector<double> errorEstimation;
      counter = 0;
      for (uint64_t i = std::get<0> (pairFiltering); i < std::get<1> (pairFiltering); i++)
        {
          errorEstimation.push_back (std::abs (x.at (counter + 1) - realSinr.at (i)));
          counter++;
        }

      meanError.at (rep) = MakeAvg ( errorEstimation );
      if (Simulator::Now () > Seconds (2.1) && Simulator::Now () < Seconds (2.3))
        {
          NS_LOG_DEBUG ("meanError " << meanError.at (rep) << " rep " << rep);
        }
      rep++;
    }

  int posMinAlpha = std::distance (meanError.begin (),std::min_element (meanError.begin (),meanError.end ()));
  double minAlpha = (posMinAlpha + 1) * 0.01;
  if (minAlpha > 0.5)
    {
      minAlpha = 0.2;
    }
  NS_LOG_DEBUG ("! " << minAlpha);

  std::vector<double> blockageTrace;
  blockageTrace.push_back (0);
  int counter = 0;
  for (uint64_t i = std::get<0> (pairFiltering); i < std::get<1> (pairFiltering); i++)
    {
      NS_LOG_DEBUG (noisySinr.at (i));
      blockageTrace.push_back ((1 - minAlpha) * blockageTrace.at (counter) + minAlpha * (noisySinr.at (i)));
      NS_LOG_DEBUG ("fff " << blockageTrace.at (counter));
      counter++;
    }

  std::vector<double> retFinalTrace;
  /* first piece */
  std::vector<double>::const_iterator firstPieceStart = noisySinr.begin ();
  std::vector<double>::const_iterator firstPieceEnd = noisySinr.begin () +  std::get<0> (pairFiltering) + 1;
  std::vector<double> firstPiece;
  firstPiece.insert (firstPiece.begin (),firstPieceStart,firstPieceEnd);
  for (std::vector<double>::const_iterator i = firstPiece.begin (); i != firstPiece.end (); i++)
    {
      NS_LOG_DEBUG ("/ " << *i);
    }

  /*last piece*/
  std::vector<double>::const_iterator lastPieceStart = noisySinr.begin () + std::get<1> (pairFiltering) + 1;
  std::vector<double>::const_iterator lastPieceEnd = noisySinr.end ();
  std::vector<double> lastPiece;

  firstPiece.insert (firstPiece.end (),blockageTrace.begin () + 1,blockageTrace.end () - 1);
  for (std::vector<double>::const_iterator i = firstPiece.begin (); i != firstPiece.end (); i++)
    {
      NS_LOG_DEBUG ("// " << *i);
    }

  firstPiece.insert (firstPiece.end (),lastPieceStart, lastPieceEnd);
  for (std::vector<double>::const_iterator i = firstPiece.begin (); i != firstPiece.end (); i++)
    {
      NS_LOG_DEBUG ("/// " << *i);
    }



  /* insert blockageTrace (from begin + 1, in order to AVOID THE TRANSIENT, to end) in the noisySinr trace,
  * after std::get<0>(pairFiltering) +1 samples,
  * which are the samples in which we estimate that a blockage occurs and the Kalmn filter is applied
  */
  //std::copy(blockageTrace.begin(),blockageTrace.end(),noisySinr.begin()+std::get<0>(pairFiltering) );
  //std::copy(blockageTrace.begin()+1,blockageTrace.end(),noisySinr.begin()+std::get<0>(pairFiltering)+1 ); // AVOID TRANSIENT

  return firstPiece;       // this noisySinr trace has already been updated with the filtered samples, where applied.
}

void
newradioEnbPhy::SetnewradioEnbCphySapUser (LteEnbCphySapUser* s)
{
  NS_LOG_FUNCTION (this);
  m_enbCphySapUser = s;
}

LteEnbCphySapProvider*
newradioEnbPhy::GetnewradioEnbCphySapProvider ()
{
  NS_LOG_FUNCTION (this);
  return m_enbCphySapProvider;
}

void
newradioEnbPhy::SetTxPower (double pow)
{
  m_txPower = pow;
}
double
newradioEnbPhy::GetTxPower () const
{
  return m_txPower;
}

void
newradioEnbPhy::SetNoiseFigure (double nf)
{
  m_noiseFigure = nf;
}
double
newradioEnbPhy::GetNoiseFigure () const
{
  return m_noiseFigure;
}

void
newradioEnbPhy::CalcChannelQualityForUe (std::vector <double> sinr, Ptr<newradioSpectrumPhy> ue)
{

}

Ptr<SpectrumValue>
newradioEnbPhy::CreateTxPowerSpectralDensity ()
{
  Ptr<SpectrumValue> psd =
    newradioSpectrumValueHelper::CreateTxPowerSpectralDensity (m_phyMacConfig, m_txPower, m_listOfSubchannels );
  return psd;
}

void
newradioEnbPhy::DoSetSubChannels ()
{

}

void
newradioEnbPhy::SetSubChannels (std::vector<int> mask )
{
  m_listOfSubchannels = mask;
  Ptr<SpectrumValue> txPsd = CreateTxPowerSpectralDensity ();
  NS_ASSERT (txPsd);
  m_downlinkSpectrumPhy->SetTxPowerSpectralDensity (txPsd);
}

Ptr<newradioSpectrumPhy>
newradioEnbPhy::GetDlSpectrumPhy () const
{
  return m_downlinkSpectrumPhy;
}

Ptr<newradioSpectrumPhy>
newradioEnbPhy::GetUlSpectrumPhy () const
{
  return m_uplinkSpectrumPhy;
}

void
newradioEnbPhy::CallPathloss ()
{
  /* THIS METHOD IS JUST USED TO LOOK THROUGH THE ALL EXPERIMENTAL SINR ADITYA'S TRACE
  EVEN WHEN THE SINR COMPUTATION IS NOT REQUIRED (SINCE THE SINR TRACE IS MADE EVERY 125MICROSECONDS) */

  Ptr<SpectrumValue> noisePsd = newradioSpectrumValueHelper::CreateNoisePowerSpectralDensity (m_phyMacConfig, m_noiseFigure);
  Ptr<SpectrumValue> totalReceivedPsd = Create <SpectrumValue> (SpectrumValue (noisePsd->GetSpectrumModel ()));

  for (std::map<uint64_t, Ptr<NetDevice> >::iterator ue = m_ueAttachedImsiMap.begin (); ue != m_ueAttachedImsiMap.end (); ++ue)
    {
      // distinguish between MC and newradioNetDevice
      Ptr<newradio::newradioUeNetDevice> ueNetDevice = DynamicCast<newradio::newradioUeNetDevice> (ue->second);
      Ptr<McUeNetDevice> mcUeDev = DynamicCast<McUeNetDevice> (ue->second);
      Ptr<newradioUePhy> uePhy;
      // get tx power
      double ueTxPower = 0;
      if (ueNetDevice != 0)
        {
          uePhy = ueNetDevice->GetPhy ();
          ueTxPower = uePhy->GetTxPower ();
        }
      else if (mcUeDev != 0)           // it may be a MC device
        {

          uePhy = mcUeDev->GetnewradioPhy ();
          ueTxPower = uePhy->GetTxPower ();
        }
      else
        {
          NS_FATAL_ERROR ("Unrecognized device");
        }
      NS_LOG_LOGIC ("UE Tx power = " << ueTxPower);
      double powerTxW = std::pow (10., (ueTxPower - 30) / 10);
      double txPowerDensity = 0;
      txPowerDensity = (powerTxW / (m_phyMacConfig->GetSystemBandwidth ()));
      NS_LOG_LOGIC ("Linear UE Tx power = " << powerTxW);
      NS_LOG_LOGIC ("System bandwidth = " << m_phyMacConfig->GetSystemBandwidth ());
      NS_LOG_LOGIC ("txPowerDensity = " << txPowerDensity);
      // create tx psd
      Ptr<SpectrumValue> txPsd =                                                        // it is the eNB that dictates the conf, m_listOfSubchannels contains all the subch
        newradioSpectrumValueHelper::CreateTxPowerSpectralDensity (m_phyMacConfig, ueTxPower, m_listOfSubchannels);
      NS_LOG_LOGIC ("TxPsd " << *txPsd);

      // get this node and remote node mobility
      Ptr<MobilityModel> enbMob = m_netDevice->GetNode ()->GetObject<MobilityModel> ();
      NS_LOG_LOGIC ("eNB mobility " << enbMob->GetPosition ());
      Ptr<MobilityModel> ueMob = ue->second->GetNode ()->GetObject<MobilityModel> ();
      NS_LOG_DEBUG ("UE mobility " << ueMob->GetPosition ());

      // compute rx psd

      // adjuts beamforming of antenna model wrt user
      Ptr<AntennaArrayModel> rxAntennaArray = DynamicCast<AntennaArrayModel> (GetDlSpectrumPhy ()->GetRxAntenna ());
      rxAntennaArray->ChangeBeamformingVectorPanel (ue->second);                                                                                // TODO check if this is the correct antenna
      Ptr<AntennaArrayModel> txAntennaArray = DynamicCast<AntennaArrayModel> (uePhy->GetDlSpectrumPhy ()->GetRxAntenna ());          // Dl, since the Ul is not actually used (TDD device)
      txAntennaArray->ChangeBeamformingVectorPanel (m_netDevice);                                                                               // TODO check if this is the correct antenna

      double pathLossDb = 0;
      if (txAntennaArray != 0)
        {
          Angles txAngles (enbMob->GetPosition (), ueMob->GetPosition ());
          double txAntennaGain = txAntennaArray->GetGainDb (txAngles);
          NS_LOG_LOGIC ("txAntennaGain = " << txAntennaGain << " dB");
          pathLossDb -= txAntennaGain;
        }
      if (rxAntennaArray != 0)
        {
          Angles rxAngles (ueMob->GetPosition (), enbMob->GetPosition ());
          double rxAntennaGain = rxAntennaArray->GetGainDb (rxAngles);
          NS_LOG_LOGIC ("rxAntennaGain = " << rxAntennaGain << " dB");
          pathLossDb -= rxAntennaGain;
        }
      if (m_propagationLoss)
        {
          if (m_losTracker != 0)               // if I am using the PL propagation model with Aditya's traces
            {
              m_losTracker->UpdateLosNlosState (ueMob,enbMob);                  // update the maps to keep trak of the real PL values, before computing the PL
            }
          double propagationGainDb = m_propagationLoss->CalcRxPower (0, ueMob, enbMob);
          NS_LOG_LOGIC ("propagationGainDb = " << propagationGainDb << " dB");
          pathLossDb -= propagationGainDb;
        }
      //NS_LOG_DEBUG ("total pathLoss = " << pathLossDb << " dB");

      double pathGainLinear = std::pow (10.0, (-pathLossDb) / 10.0);
      Ptr<SpectrumValue> rxPsd = txPsd->Copy ();
      *(rxPsd) *= pathGainLinear;

      Ptr<newradioBeamforming> beamforming = DynamicCast<newradioBeamforming> (m_spectrumPropagationLossModel);
      //beamforming->SetBeamformingVector(ue->second, m_netDevice);
      Ptr<newradioChannelMatrix> channelMatrix = DynamicCast<newradioChannelMatrix> (m_spectrumPropagationLossModel);
      Ptr<newradioChannelRaytracing> rayTracing = DynamicCast<newradioChannelRaytracing> (m_spectrumPropagationLossModel);
      if (beamforming != 0)
        {
          rxPsd = beamforming->CalcRxPowerSpectralDensity (rxPsd, ueMob, enbMob);
          NS_LOG_LOGIC ("RxPsd " << *rxPsd);
        }
      else if (channelMatrix != 0)
        {
          rxPsd = channelMatrix->CalcRxPowerSpectralDensity (rxPsd, ueMob, enbMob);
          NS_LOG_LOGIC ("RxPsd " << *rxPsd);
        }
      else if (rayTracing != 0)
        {
          rxPsd = rayTracing->CalcRxPowerSpectralDensity (rxPsd, ueMob, enbMob);
          NS_LOG_LOGIC ("RxPsd " << *rxPsd);
        }
      m_rxPsdMap[ue->first] = rxPsd;
      *totalReceivedPsd += *rxPsd;

      // set back the bf vector to the main eNB
      if (ueNetDevice != 0)
        {                                                                                                                       // target not set yet
          if ((ueNetDevice->GetTargetEnb () != m_netDevice) && (ueNetDevice->GetTargetEnb () != 0))
            {
              txAntennaArray->ChangeBeamformingVectorPanel (ueNetDevice->GetTargetEnb ());
            }
        }
      else if (mcUeDev != 0)           // it may be a MC device
        {                                                                                                                               // target not set yet
          if ((mcUeDev->GetnewradioTargetEnb () != m_netDevice) && (mcUeDev->GetnewradioTargetEnb () != 0))
            {
              txAntennaArray->ChangeBeamformingVectorPanel (mcUeDev->GetnewradioTargetEnb ());
            }
        }
      else
        {
          NS_FATAL_ERROR ("Unrecognized device");
        }

    }

  for (std::map<uint64_t, Ptr<SpectrumValue> >::iterator ue = m_rxPsdMap.begin (); ue != m_rxPsdMap.end (); ++ue)
    {
      SpectrumValue interference = *totalReceivedPsd - *(ue->second);
      NS_LOG_LOGIC ("interference " << interference);
      SpectrumValue sinr = *(ue->second) / (*noisePsd + interference);
      NS_LOG_LOGIC ("sinr " << sinr);
      double sinrAvg = Sum (sinr) / (sinr.GetSpectrumModel ()->GetNumBands ());
      NS_LOG_DEBUG ("Real SINR every 125 microseconds is: " << 10 * std::log10 (sinrAvg));
    }

  Simulator::Schedule (MicroSeconds (125), &newradioEnbPhy::CallPathloss, this);     // since one slot every 125 microseconds
}





void
newradioEnbPhy::UpdateUeSinrEstimate ()
{

  m_sinrMap.clear ();
  m_rxPsdMap.clear ();


  Ptr<SpectrumValue> noisePsd = newradioSpectrumValueHelper::CreateNoisePowerSpectralDensity (m_phyMacConfig, m_noiseFigure);
  Ptr<SpectrumValue> totalReceivedPsd = Create <SpectrumValue> (SpectrumValue (noisePsd->GetSpectrumModel ()));

  for (std::map<uint64_t, Ptr<NetDevice> >::iterator ue = m_ueAttachedImsiMap.begin (); ue != m_ueAttachedImsiMap.end (); ++ue)
    {
      // distinguish between MC and newradioNetDevice
      Ptr<newradio::newradioUeNetDevice> ueNetDevice = DynamicCast<newradio::newradioUeNetDevice> (ue->second);
      Ptr<McUeNetDevice> mcUeDev = DynamicCast<McUeNetDevice> (ue->second);
      Ptr<newradioUePhy> uePhy;
      // get tx power
      double ueTxPower = 0;
      if (ueNetDevice != 0)
        {
          uePhy = ueNetDevice->GetPhy ();
          ueTxPower = uePhy->GetTxPower ();
        }
      else if (mcUeDev != 0)           // it may be a MC device
        {

          uePhy = mcUeDev->GetnewradioPhy ();
          ueTxPower = uePhy->GetTxPower ();
        }
      else
        {
          NS_FATAL_ERROR ("Unrecognized device");
        }
      NS_LOG_LOGIC ("UE Tx power = " << ueTxPower);
      double powerTxW = std::pow (10., (ueTxPower - 30) / 10);
      double txPowerDensity = 0;
      txPowerDensity = (powerTxW / (m_phyMacConfig->GetSystemBandwidth ()));
      NS_LOG_LOGIC ("Linear UE Tx power = " << powerTxW);
      NS_LOG_LOGIC ("System bandwidth = " << m_phyMacConfig->GetSystemBandwidth ());
      NS_LOG_LOGIC ("txPowerDensity = " << txPowerDensity);
      // create tx psd
      Ptr<SpectrumValue> txPsd =                                                        // it is the eNB that dictates the conf, m_listOfSubchannels contains all the subch
        newradioSpectrumValueHelper::CreateTxPowerSpectralDensity (m_phyMacConfig, ueTxPower, m_listOfSubchannels);
      NS_LOG_LOGIC ("TxPsd " << *txPsd);

      // get this node and remote node mobility
      Ptr<MobilityModel> enbMob = m_netDevice->GetNode ()->GetObject<MobilityModel> ();
      NS_LOG_LOGIC ("eNB mobility " << enbMob->GetPosition ());
      Ptr<MobilityModel> ueMob = ue->second->GetNode ()->GetObject<MobilityModel> ();
      NS_LOG_DEBUG ("UE mobility " << ueMob->GetPosition ());

      // compute rx psd

      // adjuts beamforming of antenna model wrt user
      Ptr<AntennaArrayModel> rxAntennaArray = DynamicCast<AntennaArrayModel> (GetDlSpectrumPhy ()->GetRxAntenna ());
      rxAntennaArray->ChangeBeamformingVectorPanel (ue->second);                                                                                // TODO check if this is the correct antenna
      Ptr<AntennaArrayModel> txAntennaArray = DynamicCast<AntennaArrayModel> (uePhy->GetDlSpectrumPhy ()->GetRxAntenna ());          // Dl, since the Ul is not actually used (TDD device)
      txAntennaArray->ChangeBeamformingVectorPanel (m_netDevice);                                                                               // TODO check if this is the correct antenna

      double pathLossDb = 0;
      if (txAntennaArray != 0)
        {
          Angles txAngles (enbMob->GetPosition (), ueMob->GetPosition ());
          double txAntennaGain = txAntennaArray->GetGainDb (txAngles);
          NS_LOG_LOGIC ("txAntennaGain = " << txAntennaGain << " dB");
          pathLossDb -= txAntennaGain;
        }
      if (rxAntennaArray != 0)
        {
          Angles rxAngles (ueMob->GetPosition (), enbMob->GetPosition ());
          double rxAntennaGain = rxAntennaArray->GetGainDb (rxAngles);
          NS_LOG_LOGIC ("rxAntennaGain = " << rxAntennaGain << " dB");
          pathLossDb -= rxAntennaGain;
        }
      if (m_propagationLoss)
        {
          if (m_losTracker != 0)               // if I am using the PL propagation model with Aditya's traces
            {
              m_losTracker->UpdateLosNlosState (ueMob,enbMob);                  // update the maps to keep trak of the real PL values, before computing the PL
            }
          double propagationGainDb = m_propagationLoss->CalcRxPower (0, ueMob, enbMob);
          NS_LOG_LOGIC ("propagationGainDb = " << propagationGainDb << " dB");
          pathLossDb -= propagationGainDb;
        }
      //NS_LOG_DEBUG ("total pathLoss = " << pathLossDb << " dB");

      double pathGainLinear = std::pow (10.0, (-pathLossDb) / 10.0);
      Ptr<SpectrumValue> rxPsd = txPsd->Copy ();
      *(rxPsd) *= pathGainLinear;

      Ptr<newradioBeamforming> beamforming = DynamicCast<newradioBeamforming> (m_spectrumPropagationLossModel);
      //beamforming->SetBeamformingVector(ue->second, m_netDevice);
      Ptr<newradioChannelMatrix> channelMatrix = DynamicCast<newradioChannelMatrix> (m_spectrumPropagationLossModel);
      Ptr<newradioChannelRaytracing> rayTracing = DynamicCast<newradioChannelRaytracing> (m_spectrumPropagationLossModel);
      Ptr<newradio3gppChannel> newradio3gpp = DynamicCast<newradio3gppChannel> (m_spectrumPropagationLossModel);
      if (beamforming != 0)
        {
          rxPsd = beamforming->CalcRxPowerSpectralDensity (rxPsd, ueMob, enbMob);
          NS_LOG_LOGIC ("RxPsd " << *rxPsd);
        }
      else if (channelMatrix != 0)
        {
          rxPsd = channelMatrix->CalcRxPowerSpectralDensity (rxPsd, ueMob, enbMob);
          NS_LOG_LOGIC ("RxPsd " << *rxPsd);
        }
      else if (rayTracing != 0)
        {
          rxPsd = rayTracing->CalcRxPowerSpectralDensity (rxPsd, ueMob, enbMob);
          NS_LOG_LOGIC ("RxPsd " << *rxPsd);
        }
      else if (newradio3gpp != 0)
        {
          newradio3gpp->SetInterferenceOrDataMode (false);
          rxPsd = newradio3gpp->CalcRxPowerSpectralDensity (rxPsd, ueMob, enbMob);
          NS_LOG_LOGIC ("RxPsd " << *rxPsd);
          newradio3gpp->SetInterferenceOrDataMode (true);
        }
      m_rxPsdMap[ue->first] = rxPsd;
      *totalReceivedPsd += *rxPsd;

      // set back the bf vector to the main eNB
      if (ueNetDevice != 0)
        {                                                                                                                       // target not set yet
          if ((ueNetDevice->GetTargetEnb () != m_netDevice) && (ueNetDevice->GetTargetEnb () != 0))
            {
              txAntennaArray->ChangeBeamformingVectorPanel (ueNetDevice->GetTargetEnb ());
            }
        }
      else if (mcUeDev != 0)           // it may be a MC device
        {                                                                                                                               // target not set yet
          if ((mcUeDev->GetnewradioTargetEnb () != m_netDevice) && (mcUeDev->GetnewradioTargetEnb () != 0))
            {
              txAntennaArray->ChangeBeamformingVectorPanel (mcUeDev->GetnewradioTargetEnb ());
            }
        }
      else
        {
          NS_FATAL_ERROR ("Unrecognized device");
        }

    }

  for (std::map<uint64_t, Ptr<SpectrumValue> >::iterator ue = m_rxPsdMap.begin (); ue != m_rxPsdMap.end (); ++ue)
    {
      SpectrumValue interference = *totalReceivedPsd - *(ue->second);
      NS_LOG_LOGIC ("interference " << interference);
      SpectrumValue sinr = *(ue->second) / (*noisePsd);           // + interference);
      // we consider the SNR only!
      NS_LOG_LOGIC ("sinr " << sinr);
      double sinrAvg = Sum (sinr) / (sinr.GetSpectrumModel ()->GetNumBands ());
      NS_LOG_DEBUG ("Time " << Simulator::Now ().GetSeconds () << " CellId " << m_cellId << " UE " << ue->first << "Average SINR " << 10 * std::log10 (sinrAvg));

      if (m_noiseAndFilter)
        {
          pairDevices_t pairDevices = std::make_pair (ue->first, m_cellId);              // this is the current pair (UE-eNB)
          std::map< pairDevices_t, std::vector<double> >::iterator iteratorSinr =
            m_sinrVector.find (pairDevices);                                                     // pair [pairDevices,Sinrvalue]

          if (iteratorSinr != m_sinrVector.end ())              // this map has already been initialized, so I can add a new element for the SINR collection
            {
              if (Now ().GetMicroSeconds () <= m_transient )
                {
                  m_sinrVector.at (pairDevices).push_back (sinrAvg);                     // before transient, so just collect SINR values
                }
              else
                {
                  m_sinrVector.at (pairDevices).erase (m_sinrVector.at (pairDevices).begin ());
                  m_sinrVector.at (pairDevices).push_back (sinrAvg);                     // before transient, so just collect SINR values
                }

              NS_LOG_DEBUG ("At time " << Now ().GetMicroSeconds () << " push back the REAL SINR " << 10 * std::log10 (sinrAvg) <<
                            " for pair with CellId " << m_cellId << " and UE " << ue->first);
            }
          else               // vector is not initialized, so it means that we are still in the initial transient phase, for that pair
            {
              m_sinrVector.insert (std::pair<pairDevices_t, std::vector<double> > (pairDevices,std::vector<double> ()));
              m_sinrVector.at (pairDevices).push_back (sinrAvg);                 // push back a new SINR value
              NS_LOG_DEBUG ("At time " << Now ().GetMicroSeconds () << " first initializazion and push back the SINR " << 10 * std::log10 (sinrAvg) <<
                            " for pair with CellId " << m_cellId << " and UE " << ue->first);
            }




          std::map< pairDevices_t, std::vector<double> >::iterator iteratorFinalTrace =
            m_finalSinrVector.find (pairDevices);                                             // pair [pairDevices,Sinrvalue]
          /* INITIALIZATION OF VECTORS */
          if (iteratorFinalTrace == m_finalSinrVector.end ())
            {
              m_samplesFilter.insert (std::pair<pairDevices_t,std::pair<uint64_t,uint64_t> > (pairDevices,std::pair<uint64_t,uint64_t> ()));
              m_finalSinrVector.insert (std::pair<pairDevices_t,std::vector<double> > (pairDevices,std::vector<double> ()) );
            }


          std::map< pairDevices_t, std::vector<double> >::iterator iteratorSinrToFilter =
            m_sinrVectorToFilter.find (pairDevices);                                             // pair [pairDevices,Sinrvalue]
          /* INITIALIZATION OF VECTORS */
          if (iteratorSinrToFilter == m_sinrVectorToFilter.end ())
            {
              m_sinrVectorToFilter.insert (std::pair<pairDevices_t,std::vector<double> > (pairDevices,std::vector<double> ()));
              m_sinrVectorNoisy.insert (std::pair<pairDevices_t,std::vector<double> > (pairDevices,std::vector<double> ()));
            }

          /* generate Gaussian noise for the last SINR value (that is the current one) */
          double sinrNoisy = AddGaussianNoise (m_sinrVector.at (pairDevices).back ());


          /* UPDATE TRACE TO BE FILTERED */
          if (Now ().GetMicroSeconds () <= m_transient)
            {
              m_sinrVectorNoisy.at (pairDevices).push_back (sinrNoisy);

              // if (sinrNoisy < 0)
              // {
              //        NS_LOG_DEBUG("Old SINR value was " << 10*std::log10(sinrNoisy) << " while now is " << 10*std::log10(0.1));
              //        sinrNoisy = 0.1;
              // }
              m_sinrVectorToFilter.at (pairDevices).push_back (sinrNoisy);

            }
          else
            {
              m_sinrVectorNoisy.at (pairDevices).erase (m_sinrVectorNoisy.at (pairDevices).begin ());
              m_sinrVectorNoisy.at (pairDevices).push_back (sinrNoisy);

              // if (sinrNoisy < 0)
              // {
              //        NS_LOG_DEBUG("Old SINR value was " << 10*std::log10(sinrNoisy) << " while now is " << 10*std::log10(0.1));
              //        sinrNoisy = 0.1;
              // }

              double toPlot = *m_sinrVectorToFilter.at (pairDevices).begin ();
              double toPlotbis = sinrNoisy;
              NS_LOG_DEBUG ("(Remove SINR)  " << toPlot);
              NS_LOG_DEBUG ("(Add SINR)  " << toPlotbis);

              m_sinrVectorToFilter.at (pairDevices).erase (m_sinrVectorToFilter.at (pairDevices).begin ());
              m_sinrVectorToFilter.at (pairDevices).push_back (sinrNoisy);


            }


          if (Now ().GetMicroSeconds () > m_transient)             // apply filter only when I have a sufficiently large set of SINR samples
            {
              std::vector<double> vectorNoisy = m_sinrVectorNoisy.at (pairDevices);
              std::pair <uint64_t, uint64_t > pairFiltering = ApplyFilter (vectorNoisy);                   // find where to apply the filter, according to the variance
              m_samplesFilter.at (pairDevices) = pairFiltering;                  // where to apply the linear filter
              NS_LOG_DEBUG ("£££££££££££££££ start at sample " << std::get<0> (pairFiltering) );
              NS_LOG_DEBUG ("£££££££££££££££ end at sample " << std::get<1> (pairFiltering) );

              /* just apply filter where the SINR is too low and we are in a blockage situation */
              if (std::get<0> (m_samplesFilter.at (pairDevices)) == std::get<1> (m_samplesFilter.at (pairDevices)) )                 // if start = end
                {
                  m_finalSinrVector.at (pairDevices) = m_sinrVectorToFilter.at (pairDevices);                     // no need to apply the filter
                  NS_LOG_DEBUG ("At time " << Now ().GetMicroSeconds () << " there is no need to apply the Kalman filter for newradio eNB "
                                           << m_cellId << " and UE " << ue->first);
                }
              else
                {
                  std::vector<double> vectorToFilter = m_sinrVectorToFilter.at (pairDevices);
                  std::vector<double> sinrVector = m_sinrVector.at (pairDevices);
                  std::pair<uint64_t, uint64_t> filterPair = m_samplesFilter.at (pairDevices);
                  std::vector<double> finalTrace;
                  finalTrace = MakeFilter (vectorNoisy, sinrVector, filterPair);
                  NS_LOG_DEBUG ("finaltrace " << finalTrace.back ());
                  m_finalSinrVector.at (pairDevices) = finalTrace;
                  finalTrace.clear ();
                }


              /* the last sample in the filtered sequence is referred to the current time instant,
              * referred to the uplink reference signal that is used to build the RT in the LteEnbRrc class */
              double sampleToForward = m_finalSinrVector.at (pairDevices).back ();
              if (sampleToForward < 0)                   // this would be converted in NaN, in the log scale
                {
                  sampleToForward = 1e-20;
                }
              NS_LOG_DEBUG (" newradio eNB " << m_cellId << " reports the SINR " << 10 * std::log10 (sampleToForward) << " for UE " << ue->first);
              m_sinrMap[ue->first] = sampleToForward;                   // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< in order to FORWARD to LteEnbRrc the value of SINR for the RT

              //m_sinrVectorToFilter.at(pairDevices).erase(m_sinrVectorToFilter.at(pairDevices).begin());
              //m_sinrVectorToFilter.at(pairDevices).push_back(m_finalSinrVector.at(pairDevices).back());
              m_sinrVectorToFilter.at (pairDevices).pop_back ();
              m_sinrVectorToFilter.at (pairDevices).push_back (m_finalSinrVector.at (pairDevices).back ());
            }
          else               // before the transient is over, just forwart the (last) noisy sample, without having filtered
            {
              double sampleToForward = m_sinrVectorToFilter.at (pairDevices).back ();
              if (sampleToForward < 0)                   // this would be converted in NaN, in the log scale
                {
                  sampleToForward = 1e-20;
                }
              NS_LOG_DEBUG (" newradio eNB " << m_cellId << " FIRST reports the SINR " << 10 * std::log10 (sampleToForward) << " for UE " << ue->first);
              m_sinrMap[ue->first] = sampleToForward;                   // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< in order to FORWARD to LteEnbRrc the value of SINR for the RT
            }


          // // START PRINTING
          // if (m_cellId == 3)
          // {

          //    std::ofstream outFile ("SINR_real.txt", std::fstream::in | std::fstream::out | std::fstream::app);
          //    if (outFile.is_open())
          //    {

          //            outFile << m_sinrVector.at(pairDevices).back() << '\n';
          //            outFile.close();
          //    }
          // }
          // //END PRINTING


          // // START PRINTING
          // if (m_cellId == 3)
          // {
          //    std::ofstream outFileter ("SINR_final.txt", std::fstream::in | std::fstream::out | std::fstream::app);
          //    if (outFileter.is_open())
          //    {
          //            outFileter << m_sinrVectorToFilter.at(pairDevices).back() << '\n';
          //            outFileter.close();
          //    }
          // }
          // // END PRINTING

          //            // START PRINTING
          // if (m_cellId == 3)
          // {
          //    std::ofstream outFileter ("SINR_noisy.txt", std::fstream::in | std::fstream::out | std::fstream::app);
          //    if (outFileter.is_open())
          //    {
          //            outFileter << sinrNoisy << '\n';
          //            outFileter.close();
          //    }
          // }
          // // END PRINTING




          /* after the SINR sample is forwarded, I need to REFRESH all the maps, so that
          * a new SINR sequence can be created, to generate a new SINR sample
          * for the next RT
          */
          m_finalSinrVector.erase (pairDevices);
          m_samplesFilter.erase (pairDevices);
          NS_LOG_DEBUG ("ERASE MAPS FOR RT");
        }
      else           // noise and filtering processes are not applied!
        {
          m_sinrMap[ue->first] = sinrAvg;
        }

    }


  if (m_roundFromLastUeSinrUpdate >= (m_ueUpdateSinrPeriod / m_updateSinrPeriod))
    {
      m_roundFromLastUeSinrUpdate = 0;
      for (std::map<uint64_t, Ptr<NetDevice> >::iterator ue = m_ueAttachedImsiMap.begin (); ue != m_ueAttachedImsiMap.end (); ++ue)
        {
          // distinguish between MC and newradioNetDevice
          Ptr<newradio::newradioUeNetDevice> ueNetDevice = DynamicCast<newradio::newradioUeNetDevice> (ue->second);
          Ptr<McUeNetDevice> mcUeDev = DynamicCast<McUeNetDevice> (ue->second);
          Ptr<newradioUePhy> uePhy;
          if (ueNetDevice != 0)
            {
              uePhy = ueNetDevice->GetPhy ();
            }
          else if (mcUeDev != 0)               // it may be a MC device
            {
              uePhy = mcUeDev->GetnewradioPhy ();
            }
          uePhy->UpdateSinrEstimate (m_cellId, m_sinrMap.find (ue->first)->second);
        }
    }
  else
    {
      m_roundFromLastUeSinrUpdate++;
    }

  LteEnbCphySapUser::UeAssociatedSinrInfo info;
  info.ueImsiSinrMap = m_sinrMap;
  info.componentCarrierId = m_componentCarrierId;
  m_enbCphySapUser->UpdateUeSinrEstimate (info);


  Simulator::Schedule (MicroSeconds (m_updateSinrPeriod), &newradioEnbPhy::UpdateUeSinrEstimate, this);     // recall after m_updateSinrPeriod microseconds
}

void
newradioEnbPhy::StartSubFrame (void)
{
  NS_LOG_FUNCTION (this);

  m_lastSfStart = Simulator::Now ();

  m_currSfAllocInfo = m_sfAllocInfo[m_sfNum];
  //m_currSfNumSlots = m_currSfAllocInfo.m_dlSlotAllocInfo.size () + m_currSfAllocInfo.m_ulSlotAllocInfo.size ();
  m_currSfNumSlots = m_currSfAllocInfo.m_slotAllocInfo.size ();

  NS_ASSERT ((m_currSfAllocInfo.m_sfnSf.m_frameNum == m_frameNum)
             && (m_currSfAllocInfo.m_sfnSf.m_sfNum == m_sfNum));

  if (m_sfNum == 0)                     // send MIB at the beginning of each frame
    {
      LteRrcSap::MasterInformationBlock mib;
      mib.dlBandwidth = (uint8_t)4;
      mib.systemFrameNumber = 1;
      Ptr<newradioMibMessage> mibMsg = Create<newradioMibMessage> ();
      mibMsg->SetMib (mib);
      if (m_controlMessageQueue.empty ())
        {
          std::list<Ptr<newradioControlMessage> > l;
          m_controlMessageQueue.push_back (l);
        }
      m_controlMessageQueue.at (0).push_back (mibMsg);
    }
  else if (m_sfNum == 5)        // send SIB at beginning of second half-frame
    {
      Ptr<newradioSib1Message> msg = Create<newradioSib1Message> ();
      msg->SetSib1 (m_sib1);
      m_controlMessageQueue.at (0).push_back (msg);
    }

  StartSlot ();
}

void
newradioEnbPhy::StartSlot (void)
{
  // TODO uncomment this lines for non-omni transmission of ctrl channels
  // This does not make any difference, since error model for the control messages
  // is not supported and ctrl messages are always received correctly
  // assume the control signal is omni
  // Ptr<AntennaArrayModel> antennaArray = DynamicCast<AntennaArrayModel> (GetDlSpectrumPhy ()->GetRxAntenna());
  // antennaArray->ChangeToOmniTx ();

  NS_LOG_FUNCTION (this);

  SlotAllocInfo currSlot;

  /*uint8_t slotInd = 0;
  if (m_slotNum >= m_currSfAllocInfo.m_dlSlotAllocInfo.size ())
  {
          if (m_currSfAllocInfo.m_ulSlotAllocInfo.size () > 0)
          {
                  slotInd = m_slotNum - m_currSfAllocInfo.m_dlSlotAllocInfo.size ();
                  currSlot = m_currSfAllocInfo.m_ulSlotAllocInfo[slotInd];
                  m_currSymStart = currSlot.m_dci.m_symStart;
          }
  }
  else
  {
          if (m_currSfAllocInfo.m_ulSlotAllocInfo.size () > 0)
          {
                  slotInd = m_slotNum;
                  currSlot = m_currSfAllocInfo.m_dlSlotAllocInfo[slotInd];
                  m_currSymStart = currSlot.m_dci.m_symStart;
          }
  }*/

  //slotInd = m_slotNum;
  currSlot = m_currSfAllocInfo.m_slotAllocInfo[m_slotNum];
  m_currSymStart = currSlot.m_dci.m_symStart;

  SfnSf sfn = SfnSf (m_frameNum, m_sfNum, m_slotNum);
  m_harqPhyModule->SubframeIndication (sfn);  // trigger HARQ module

  std::list <Ptr<newradioControlMessage > > dciMsgList;

  Time guardPeriod;
  Time slotPeriod;

  if (m_slotNum == 0)       // DL control slot
    {
      // get control messages to be transmitted in DL-Control period
      std::list <Ptr<newradioControlMessage > > ctrlMsgs = GetControlMessages ();
      //std::list <Ptr<newradioControlMessage > >::iterator it = ctrlMsgs.begin ();
      // find all DL/UL DCI elements and create DCI messages to be transmitted in DL control period
      for (unsigned islot = 0; islot < m_currSfAllocInfo.m_slotAllocInfo.size (); islot++)
        {
          if (m_currSfAllocInfo.m_slotAllocInfo[islot].m_slotType != SlotAllocInfo::CTRL
              && m_currSfAllocInfo.m_slotAllocInfo[islot].m_tddMode == SlotAllocInfo::DL_slotAllocInfo)
            {
              DciInfoElementTdma &dciElem = m_currSfAllocInfo.m_slotAllocInfo[islot].m_dci;
              NS_ASSERT (dciElem.m_format == DciInfoElementTdma::DL_dci);
              if (dciElem.m_tbSize > 0)
                {
                  Ptr<newradioTdmaDciMessage> dciMsg = Create<newradioTdmaDciMessage> ();
                  dciMsg->SetDciInfoElement (dciElem);
                  dciMsg->SetSfnSf (sfn);
                  dciMsgList.push_back (dciMsg);
                  ctrlMsgs.push_back (dciMsg);
                }
            }
        }

      unsigned ulSfNum = (m_sfNum + m_phyMacConfig->GetUlSchedDelay ()) % m_phyMacConfig->GetSubframesPerFrame ();
      for (unsigned islot = 0; islot < m_sfAllocInfo[ulSfNum].m_slotAllocInfo.size (); islot++)
        {
          if (m_sfAllocInfo[ulSfNum].m_slotAllocInfo[islot].m_slotType != SlotAllocInfo::CTRL
              && m_sfAllocInfo[ulSfNum].m_slotAllocInfo[islot].m_tddMode == SlotAllocInfo::UL_slotAllocInfo)
            {
              DciInfoElementTdma &dciElem = m_sfAllocInfo[ulSfNum].m_slotAllocInfo[islot].m_dci;
              NS_ASSERT (dciElem.m_format == DciInfoElementTdma::UL_dci);
              if (dciElem.m_tbSize > 0)
                {
                  Ptr<newradioTdmaDciMessage> dciMsg = Create<newradioTdmaDciMessage> ();
                  dciMsg->SetDciInfoElement (dciElem);
                  dciMsg->SetSfnSf (sfn);
                  //dciMsgList.push_back (dciMsg);
                  ctrlMsgs.push_back (dciMsg);
                }
            }
        }

      // TX control period
      slotPeriod = NanoSeconds (1000.0 * m_phyMacConfig->GetSymbolPeriod () * m_phyMacConfig->GetDlCtrlSymbols ());
      NS_LOG_DEBUG ("ENB " << m_cellId << " TXing DL CTRL frame " << m_frameNum << " subframe " << (unsigned)m_sfNum << " symbols "
                           << (unsigned)currSlot.m_dci.m_symStart << "-" << (unsigned)(currSlot.m_dci.m_symStart + currSlot.m_dci.m_numSym - 1)
                           << "\t start " << Simulator::Now () << " end " << Simulator::Now () + slotPeriod - NanoSeconds (1.0));
      SendCtrlChannels (ctrlMsgs, slotPeriod - NanoSeconds (1.0));         // -1 ns ensures control ends before data period
    }
  else if (m_slotNum == m_currSfNumSlots - 1)       // UL control slot
    {
      slotPeriod = NanoSeconds (1000.0 * m_phyMacConfig->GetSymbolPeriod () * m_phyMacConfig->GetUlCtrlSymbols ());
      NS_LOG_DEBUG ("ENB " << m_cellId << " RXing UL CTRL frame " << m_frameNum << " subframe " << (unsigned)m_sfNum << " symbols "
                           << (unsigned)currSlot.m_dci.m_symStart << "-" << (unsigned)(currSlot.m_dci.m_symStart + currSlot.m_dci.m_numSym - 1)
                           << "\t start " << Simulator::Now () << " end " << Simulator::Now () + slotPeriod);
    }
  else if (currSlot.m_tddMode == SlotAllocInfo::DL_slotAllocInfo)                 // transmit DL slot
    {
      slotPeriod = NanoSeconds (1000.0 * m_phyMacConfig->GetSymbolPeriod () * currSlot.m_dci.m_numSym);
      NS_ASSERT (currSlot.m_tddMode == SlotAllocInfo::DL_slotAllocInfo);
      //NS_LOG_DEBUG ("Slot " << m_slotNum << " scheduled for Downlink");
      //			if (m_prevSlotDir == SlotAllocInfo::UL_slotAllocInfo)  // if curr slot == DL and prev slot == UL
      //			{
      //				guardPeriod = NanoSeconds (1000.0 * m_phyMacConfig->GetGuardPeriod ());
      //			}
      Ptr<PacketBurst> pktBurst = GetPacketBurst (SfnSf (m_frameNum, m_sfNum, currSlot.m_dci.m_symStart));
      if (pktBurst && pktBurst->GetNPackets () > 0)
        {
          std::list< Ptr<Packet> > pkts = pktBurst->GetPackets ();
          newradioMacPduTag macTag;
          pkts.front ()->PeekPacketTag (macTag);
          NS_ASSERT ((macTag.GetSfn ().m_sfNum == m_sfNum) && (macTag.GetSfn ().m_slotNum == currSlot.m_dci.m_symStart));
        }
      else
        {
          // sometimes the UE will be scheduled when no data is queued
          // in this case, send an empty PDU
          newradioMacPduTag tag (SfnSf (m_frameNum, m_sfNum, currSlot.m_dci.m_symStart));
          Ptr<Packet> emptyPdu = Create <Packet> ();
          newradioMacPduHeader header;
          MacSubheader subheader (3, 0);                // lcid = 3, size = 0
          header.AddSubheader (subheader);
          emptyPdu->AddHeader (header);
          emptyPdu->AddPacketTag (tag);
          LteRadioBearerTag bearerTag (currSlot.m_dci.m_rnti, 3, 0);
          emptyPdu->AddPacketTag (bearerTag);
          pktBurst = CreateObject<PacketBurst> ();
          pktBurst->AddPacket (emptyPdu);
        }
      NS_LOG_DEBUG ("ENB " << m_cellId << " TXing DL DATA frame " << m_frameNum << " subframe " << (unsigned)m_sfNum << " symbols "
                           << (unsigned)currSlot.m_dci.m_symStart << "-" << (unsigned)(currSlot.m_dci.m_symStart + currSlot.m_dci.m_numSym - 1)
                           << "\t start " << Simulator::Now () + NanoSeconds (1.0) << " end " << Simulator::Now () + slotPeriod - NanoSeconds (2.0));
      Simulator::Schedule (NanoSeconds (1.0), &newradioEnbPhy::SendDataChannels, this, pktBurst, slotPeriod - NanoSeconds (2.0), currSlot);
    }
  else if (currSlot.m_tddMode == SlotAllocInfo::UL_slotAllocInfo)        // receive UL slot
    {
      slotPeriod = NanoSeconds (1000.0 * m_phyMacConfig->GetSymbolPeriod () * currSlot.m_dci.m_numSym);
      //NS_LOG_DEBUG ("Slot " << (uint8_t)m_slotNum << " scheduled for Uplink");
      m_downlinkSpectrumPhy->AddExpectedTb (currSlot.m_dci.m_rnti, currSlot.m_dci.m_ndi, currSlot.m_dci.m_tbSize,
                                            currSlot.m_dci.m_mcs, m_channelChunks, currSlot.m_dci.m_harqProcess, currSlot.m_dci.m_rv, false,
                                            currSlot.m_dci.m_symStart, currSlot.m_dci.m_numSym);

      for (uint8_t i = 0; i < m_deviceMap.size (); i++)
        {
          Ptr<newradio::newradioUeNetDevice> ueDev = DynamicCast<newradio::newradioUeNetDevice> (m_deviceMap.at (i));
          Ptr<McUeNetDevice> mcUeDev = DynamicCast<McUeNetDevice> (m_deviceMap.at (i));
          uint64_t ueRnti = (ueDev != 0) ? (ueDev->GetPhy ()->GetRnti ()) : (mcUeDev->GetnewradioPhy ()->GetRnti ());
          Ptr<NetDevice> associatedEnb = (ueDev != 0) ? (ueDev->GetTargetEnb ()) : (mcUeDev->GetnewradioTargetEnb ());

          NS_LOG_DEBUG ("Scheduled rnti: " << currSlot.m_rnti << " ue rnti: " << ueRnti
                                           << " target eNB " << associatedEnb << " this eNB " << m_netDevice);

          if (currSlot.m_rnti == ueRnti && m_netDevice == associatedEnb)
            {
              //NS_LOG_DEBUG ("Change Beamforming Vector");
              Ptr<AntennaArrayModel> antennaArray = DynamicCast<AntennaArrayModel> (GetDlSpectrumPhy ()->GetRxAntenna ());
              antennaArray->ChangeBeamformingVectorPanel (m_deviceMap.at (i));
              break;
            }
        }

      NS_LOG_DEBUG ("ENB " << m_cellId << " RXing UL DATA frame " << m_frameNum << " subframe " << (unsigned)m_sfNum << " symbols "
                           << (unsigned)currSlot.m_dci.m_symStart << "-" << (unsigned)(currSlot.m_dci.m_symStart + currSlot.m_dci.m_numSym - 1)
                           << "\t start " << Simulator::Now () << " end " << Simulator::Now () + slotPeriod );
    }

  m_prevSlotDir = currSlot.m_tddMode;

  m_phySapUser->SubframeIndication (SfnSf (m_frameNum, m_sfNum, m_slotNum));        // trigger MAC

  Simulator::Schedule (slotPeriod, &newradioEnbPhy::EndSlot, this);
}

void
newradioEnbPhy::EndSlot (void)
{
  NS_LOG_FUNCTION (this << Simulator::Now ().GetSeconds ());

  //Ptr<AntennaArrayModel> antennaArray = DynamicCast<AntennaArrayModel> (GetDlSpectrumPhy ()->GetRxAntenna());
  //antennaArray->ChangeToOmniTx ();

  if (m_slotNum == m_currSfNumSlots - 1)
    {
      m_slotNum = 0;
      EndSubFrame ();
    }
  else
    {
      Time nextSlotStart;
      //uint8_t slotInd = m_slotNum+1;
      /*if (slotInd >= m_currSfAllocInfo.m_slotAllocInfo.size ())
      {
              if (m_currSfAllocInfo.m_slotAllocInfo.size () > 0)
              {
                      slotInd = slotInd - m_currSfAllocInfo.m_slotAllocInfo.size ();
                      nextSlotStart = NanoSeconds (1000.0 * m_phyMacConfig->GetSymbolPeriod () *
                                                   m_currSfAllocInfo.m_ulSlotAllocInfo[slotInd].m_dci.m_symStart);
              }
      }
      else
      {
              if (m_currSfAllocInfo.m_slotAllocInfo.size () > 0)
              {
                      nextSlotStart = NanoSeconds (1000.0 * m_phyMacConfig->GetSymbolPeriod () *
                                                   m_currSfAllocInfo.m_slotAllocInfo[slotInd].m_dci.m_symStart);
              }
      }*/
      m_slotNum++;
      nextSlotStart = NanoSeconds (1000.0 * m_phyMacConfig->GetSymbolPeriod () *
                                   m_currSfAllocInfo.m_slotAllocInfo[m_slotNum].m_dci.m_symStart);
      Simulator::Schedule (nextSlotStart + m_lastSfStart - Simulator::Now (), &newradioEnbPhy::StartSlot, this);
    }
}

void
newradioEnbPhy::EndSubFrame (void)
{
  NS_LOG_FUNCTION (this << Simulator::Now ().GetSeconds ());

  Time sfStart = m_lastSfStart + m_sfPeriod - Simulator::Now ();
  m_slotNum = 0;
  if (m_sfNum == m_phyMacConfig->GetSubframesPerFrame () - 1)
    {
      m_sfNum = 0;
//		if (m_frameNum == 1023)
//		{
//			m_frameNum = 0;
//		}
//		else
//		{
//			m_frameNum++;
//		}
      m_frameNum++;
    }
  else
    {
      m_sfNum++;
    }

  Simulator::Schedule (sfStart, &newradioEnbPhy::StartSubFrame, this);
}

void
newradioEnbPhy::SendDataChannels (Ptr<PacketBurst> pb, Time slotPrd, SlotAllocInfo& slotInfo)
{
  if (slotInfo.m_isOmni)
    {
      Ptr<AntennaArrayModel> antennaArray = DynamicCast<AntennaArrayModel> (GetDlSpectrumPhy ()->GetRxAntenna ());
      antennaArray->ChangeToOmniTx ();
    }
  else
    {     // update beamforming vectors (currently supports 1 user only)
      //std::map<uint16_t, std::vector<unsigned> >::iterator ueRbIt = slotInfo.m_ueRbMap.begin();
      //uint16_t rnti = ueRbIt->first;
      for (uint8_t i = 0; i < m_deviceMap.size (); i++)
        {
          uint64_t ueRnti = 0;
          Ptr<newradio::newradioUeNetDevice> ueDev = m_deviceMap.at (i)->GetObject<newradio::newradioUeNetDevice> ();
          Ptr<NetDevice> associatedEnb = 0;
          if (ueDev != 0)
            {
              ueRnti = ueDev->GetPhy ()->GetRnti ();
              associatedEnb = ueDev->GetTargetEnb ();
            }
          else
            {
              Ptr<McUeNetDevice> ueMcDev = m_deviceMap.at (i)->GetObject<McUeNetDevice> ();
              ueRnti = ueMcDev->GetnewradioPhy ()->GetRnti ();
              associatedEnb = ueMcDev->GetnewradioTargetEnb ();
            }

          NS_LOG_DEBUG ("Scheduled rnti: " << slotInfo.m_dci.m_rnti << " ue rnti: " << ueRnti
                                           << " target eNB " << associatedEnb << " this eNB " << m_netDevice);
          if (slotInfo.m_dci.m_rnti == ueRnti && m_netDevice == associatedEnb)
            {
              NS_LOG_DEBUG ("Change Beamforming Vector");
              Ptr<AntennaArrayModel> antennaArray = DynamicCast<AntennaArrayModel> (GetDlSpectrumPhy ()->GetRxAntenna ());
              antennaArray->ChangeBeamformingVectorPanel (m_deviceMap.at (i));
              break;
            }

        }
    }

  /*
  if (!slotInfo.m_isOmni && !slotInfo.m_ueRbMap.empty ())
  {
          Ptr<AntennaArrayModel> antennaArray = DynamicCast<AntennaArrayModel> (GetDlSpectrumPhy ()->GetRxAntenna());
           //set beamforming vector;
           //for ENB, you can choose 64 antenna with 0-15 sectors, or 4 antenna with 0-3 sectors;
           //input is (sector, antenna number)
          antennaArray->SetSector (0,64);
  }
  */

  std::list<Ptr<newradioControlMessage> > ctrlMsgs;
  m_downlinkSpectrumPhy->StartTxDataFrames (pb, ctrlMsgs, slotPrd, slotInfo.m_slotIdx);
}

void
newradioEnbPhy::SendCtrlChannels (std::list<Ptr<newradioControlMessage> > ctrlMsgs, Time slotPrd)
{
  /* Send Ctrl messages*/
  NS_LOG_FUNCTION (this << "Send Ctrl");
  m_downlinkSpectrumPhy->StartTxDlControlFrames (ctrlMsgs, slotPrd);
}

bool
newradioEnbPhy::AddUePhy (uint64_t imsi, Ptr<NetDevice> ueDevice)
{
  NS_LOG_FUNCTION (this << imsi);
  std::set <uint64_t>::iterator it;
  it = m_ueAttached.find (imsi);

  if (it == m_ueAttached.end ())
    {
      m_ueAttached.insert (imsi);
      m_deviceMap.push_back (ueDevice);
      m_ueAttachedImsiMap[imsi] = ueDevice;
      return (true);
    }
  else
    {
      NS_LOG_ERROR ("Programming error...UE already attached");
      return (false);
    }
}

void
newradioEnbPhy::PhyDataPacketReceived (Ptr<Packet> p)
{
  Simulator::ScheduleWithContext (m_netDevice->GetNode ()->GetId (),
                                  MicroSeconds (m_phyMacConfig->GetTbDecodeLatency ()),
                                  &newradioEnbPhySapUser::ReceivePhyPdu,
                                  m_phySapUser,
                                  p);
//		m_phySapUser->ReceivePhyPdu(p);
}

void
newradioEnbPhy::GenerateDataCqiReport (const SpectrumValue& sinr)
{
  NS_LOG_LOGIC ("Sinr from DataCqiReport = " << sinr);
  double sinrAvg = Sum (sinr) / (sinr.GetSpectrumModel ()->GetNumBands ());
  NS_LOG_INFO ("Average SINR on DataCqiReport " << 10 * std::log10 (sinrAvg));

  Values::const_iterator it;
  newradioMacSchedSapProvider::SchedUlCqiInfoReqParameters ulcqi;
  ulcqi.m_ulCqi.m_type = UlCqiInfo::PUSCH;
  int i = 0;
  for (it = sinr.ConstValuesBegin (); it != sinr.ConstValuesEnd (); it++)
    {
//      double sinrdb = 10 * std::log10 ((*it));
      //       NS_LOG_DEBUG ("ULCQI RB " << i << " value " << sinrdb);
      // convert from double to fixed point notaltion Sxxxxxxxxxxx.xxx
//      int16_t sinrFp = LteFfConverter::double2fpS11dot3 (sinrdb);
      ulcqi.m_ulCqi.m_sinr.push_back (*it);
      i++;
    }

  // here we use the start symbol index of the slot in place of the slot index because the absolute UL slot index is
  // not known to the scheduler when m_allocationMap gets populated
  ulcqi.m_sfnSf = SfnSf (m_frameNum, m_sfNum, m_currSymStart);
  SpectrumValue newSinr = sinr;
  m_ulSinrTrace (0, newSinr, newSinr);
  m_phySapUser->UlCqiReport (ulcqi);
}


void
newradioEnbPhy::PhyCtrlMessagesReceived (std::list<Ptr<newradioControlMessage> > msgList)
{
  std::list<Ptr<newradioControlMessage> >::iterator ctrlIt = msgList.begin ();

  while (ctrlIt != msgList.end ())
    {
      Ptr<newradioControlMessage> msg = (*ctrlIt);

      if (msg->GetMessageType () == newradioControlMessage::DL_CQI)
        {
          NS_LOG_INFO ("ENB " << m_cellId << " received CQI");
          m_phySapUser->ReceiveControlMessage (msg);
        }
      else if (msg->GetMessageType () == newradioControlMessage::BSR)
        {
          NS_LOG_INFO ("ENB " << m_cellId << " received BSR");
          m_phySapUser->ReceiveControlMessage (msg);
        }
      else if (msg->GetMessageType () == newradioControlMessage::RACH_PREAMBLE)
        {
          NS_LOG_INFO ("ENB " << m_cellId << " received RACH_PREAMBLE");
          NS_ASSERT (m_cellId > 0);
          Ptr<newradioRachPreambleMessage> rachPreamble = DynamicCast<newradioRachPreambleMessage> (msg);
          m_phySapUser->ReceiveRachPreamble (rachPreamble->GetRapId ());
        }
      else if (msg->GetMessageType () == newradioControlMessage::DL_HARQ)
        {
          Ptr<newradioDlHarqFeedbackMessage> dlharqMsg = DynamicCast<newradioDlHarqFeedbackMessage> (msg);
          DlHarqInfo dlharq = dlharqMsg->GetDlHarqFeedback ();
          // check whether the UE is connected
          if (m_ueAttachedRnti.find (dlharq.m_rnti) != m_ueAttachedRnti.end ())
            {
              m_phySapUser->ReceiveControlMessage (msg);
            }
        }

      ctrlIt++;
    }

}

uint32_t
newradioEnbPhy::GetAbsoluteSubframeNo ()
{
  return ((m_frameNum - 1) * (m_phyMacConfig->GetSubframesPerFrame () * m_phyMacConfig->GetSlotsPerSubframe ()) + m_slotNum);
}

////////////////////////////////////////////////////////////
/////////                     sap                 /////////
///////////////////////////////////////////////////////////

void
newradioEnbPhy::DoSetBandwidth (uint8_t ulBandwidth, uint8_t dlBandwidth)
{
  NS_LOG_FUNCTION (this << (uint32_t) ulBandwidth << (uint32_t) dlBandwidth);
}

void
newradioEnbPhy::DoSetEarfcn (uint16_t ulEarfcn, uint16_t dlEarfcn)
{
  NS_LOG_FUNCTION (this << ulEarfcn << dlEarfcn);
}


void
newradioEnbPhy::DoAddUe (uint16_t rnti)
{
  NS_LOG_FUNCTION (this << rnti);
  bool success = AddUePhy (rnti);
  NS_ASSERT_MSG (success, "AddUePhy() failed");

}

bool
newradioEnbPhy::AddUePhy (uint16_t rnti)
{
  NS_LOG_FUNCTION (this << rnti);
  std::set <uint16_t>::iterator it;
  it = m_ueAttachedRnti.find (rnti);
  if (it == m_ueAttachedRnti.end ())
    {
      m_ueAttachedRnti.insert (rnti);
      return (true);
    }
  else
    {
      NS_LOG_ERROR ("UE already attached");
      return (false);
    }
}

void
newradioEnbPhy::DoRemoveUe (uint16_t rnti)
{
  std::set <uint16_t>::iterator it = m_ueAttachedRnti.find (rnti);
  if (it != m_ueAttachedRnti.end ())
    {
      m_ueAttachedRnti.erase (it);
    }
  else
    {
      NS_FATAL_ERROR ("Impossible to remove UE, not attached!");
    }
  NS_LOG_FUNCTION (this << rnti);
}

void
newradioEnbPhy::DoSetPa (uint16_t rnti, double pa)
{
  NS_LOG_FUNCTION (this << rnti);
}

void
newradioEnbPhy::DoSetTransmissionMode (uint16_t  rnti, uint8_t txMode)
{
  NS_LOG_FUNCTION (this << rnti << (uint16_t)txMode);
  // UL supports only SISO MODE
}

void
newradioEnbPhy::DoSetSrsConfigurationIndex (uint16_t  rnti, uint16_t srcCi)
{
  NS_LOG_FUNCTION (this);
}


void
newradioEnbPhy::DoSetMasterInformationBlock (LteRrcSap::MasterInformationBlock mib)
{
  NS_LOG_FUNCTION (this);
  //m_mib = mib;
}


void
newradioEnbPhy::DoSetSystemInformationBlockType1 (LteRrcSap::SystemInformationBlockType1 sib1)
{
  NS_LOG_FUNCTION (this);
  m_sib1 = sib1;
}

int8_t
newradioEnbPhy::DoGetReferenceSignalPower () const
{
  NS_LOG_FUNCTION (this);
  return m_txPower;
}

void
newradioEnbPhy::SetPhySapUser (newradioEnbPhySapUser* ptr)
{
  m_phySapUser = ptr;
}

void
newradioEnbPhy::SetHarqPhyModule (Ptr<newradioHarqPhy> harq)
{
  m_harqPhyModule = harq;
}

void
newradioEnbPhy::ReceiveUlHarqFeedback (UlHarqInfo mes)
{
  NS_LOG_FUNCTION (this);
  // forward to scheduler
  //
  if (m_ueAttachedRnti.find (mes.m_rnti) != m_ueAttachedRnti.end ())
    {
      m_phySapUser->UlHarqFeedback (mes);
    }
}

} // namespace newradio

} // namespace ns3
