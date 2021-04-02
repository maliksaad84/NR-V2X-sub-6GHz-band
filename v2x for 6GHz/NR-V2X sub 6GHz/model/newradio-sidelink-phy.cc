#include "newradio-sidelink-phy.h"
#include <ns3/newradio-spectrum-value-helper.h>
#include <ns3/newradio-mac-pdu-tag.h>
#include <ns3/newradio-mac-pdu-header.h>
#include <ns3/double.h>
#include <ns3/pointer.h>

namespace ns3 {

namespace millicar {

MacSidelinkMemberPhySapProvider::MacSidelinkMemberPhySapProvider (Ptr<newradioSidelinkPhy> phy)
  : m_phy (phy)
{

}

void
MacSidelinkMemberPhySapProvider::AddTransportBlock (Ptr<PacketBurst> pb, newradio::SlotAllocInfo info)
{
  m_phy->DoAddTransportBlock (pb, info);
}

void
MacSidelinkMemberPhySapProvider::PrepareForReception (uint16_t rnti)
{
  m_phy->DoPrepareForReceptionFrom (rnti);
}

//-----------------------------------------------------------------------

NS_LOG_COMPONENT_DEFINE ("newradioSidelinkPhy");

NS_OBJECT_ENSURE_REGISTERED (newradioSidelinkPhy);

newradioSidelinkPhy::newradioSidelinkPhy ()
{
  NS_LOG_FUNCTION (this);
  NS_FATAL_ERROR ("This constructor should not be called");
}

newradioSidelinkPhy::newradioSidelinkPhy (Ptr<newradioSidelinkSpectrumPhy> spectrumPhy, Ptr<newradio::newradioPhyMacCommon> confParams)
{
  NS_LOG_FUNCTION (this);
  m_sidelinkSpectrumPhy = spectrumPhy;
  m_phyMacConfig = confParams;

  // create the PHY SAP provider
  m_phySapProvider = new MacSidelinkMemberPhySapProvider (this);

  // create the noise PSD
  Ptr<SpectrumValue> noisePsd = newradio::newradioSpectrumValueHelper::CreateNoisePowerSpectralDensity (m_phyMacConfig, m_noiseFigure);
  m_sidelinkSpectrumPhy->SetNoisePowerSpectralDensity (noisePsd);

  // schedule the first slot
  Simulator::ScheduleNow (&newradioSidelinkPhy::StartSlot, this, newradio::SfnSf (0, 0, 0));
}

newradioSidelinkPhy::~newradioSidelinkPhy ()
{
  NS_LOG_FUNCTION (this);
}

TypeId
newradioSidelinkPhy::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::newradioSidelinkPhy")
    .SetParent<Object> ()
    .AddConstructor<newradioSidelinkPhy> ()
    .AddAttribute ("TxPower",
                   "Transmission power in dBm",
                   DoubleValue (30.0),
                   MakeDoubleAccessor (&newradioSidelinkPhy::SetTxPower,
                                       &newradioSidelinkPhy::GetTxPower),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("NoiseFigure",
                    "Loss (dB) in the Signal-to-Noise-Ratio due to non-idealities in the receiver."
                    " According to Wikipedia (http://en.wikipedia.org/wiki/Noise_figure), this is "
                    "\"the difference in decibels (dB) between"
                    " the noise output of the actual receiver to the noise output of an "
                    " ideal receiver with the same overall gain and bandwidth when the receivers "
                    " are connected to sources at the standard noise temperature T0.\" "
                   "In this model, we consider T0 = 290K.",
                    DoubleValue (5.0),
                    MakeDoubleAccessor (&newradioSidelinkPhy::SetNoiseFigure,
                                        &newradioSidelinkPhy::GetNoiseFigure),
                    MakeDoubleChecker<double> ());
  return tid;
}

void
newradioSidelinkPhy::DoInitialize (void)
{
  NS_LOG_FUNCTION (this);
}

void
newradioSidelinkPhy::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  delete m_phySapProvider;
}

void
newradioSidelinkPhy::SetTxPower (double power)
{
  m_txPower = power;
}
double
newradioSidelinkPhy::GetTxPower () const
{
  return m_txPower;
}

void
newradioSidelinkPhy::SetNoiseFigure (double nf)
{
  m_noiseFigure = nf;

  // update the noise PSD
  Ptr<SpectrumValue> noisePsd = newradio::newradioSpectrumValueHelper::CreateNoisePowerSpectralDensity (m_phyMacConfig, m_noiseFigure);
  m_sidelinkSpectrumPhy->SetNoisePowerSpectralDensity (noisePsd);
}

double
newradioSidelinkPhy::GetNoiseFigure () const
{
  return m_noiseFigure;
}

Ptr<newradioSidelinkSpectrumPhy>
newradioSidelinkPhy::GetSpectrumPhy () const
{
  return m_sidelinkSpectrumPhy;
}

Ptr<newradio::newradioPhyMacCommon>
newradioSidelinkPhy::GetConfigurationParameters (void) const
{
  return m_phyMacConfig;
}

newradioSidelinkPhySapProvider*
newradioSidelinkPhy::GetPhySapProvider () const
{
  NS_LOG_FUNCTION (this);
  return m_phySapProvider;
}

void
newradioSidelinkPhy::SetPhySapUser (newradioSidelinkPhySapUser* sap)
{
  NS_LOG_FUNCTION (this);
  m_phySapUser = sap;
}

void
newradioSidelinkPhy::DoAddTransportBlock (Ptr<PacketBurst> pb, newradio::SlotAllocInfo info)
{
  // create a new entry for the PHY buffer
  PhyBufferEntry e = std::make_pair (pb, info);

  // add the new entry to the buffer
  m_phyBuffer.push_back (e);
}

void
newradioSidelinkPhy::StartSlot (newradio::SfnSf timingInfo)
{
   NS_LOG_FUNCTION (this << " frame " << timingInfo.m_frameNum << " subframe " << timingInfo.m_sfNum << " slot " << timingInfo.m_slotNum);

  // trigger the MAC
  m_phySapUser->SlotIndication (timingInfo);

  while (m_phyBuffer.size () != 0)
  {
    uint8_t usedSymbols = 0; // the symbol index

    // retrieve the first element in the list
    Ptr<PacketBurst> pktBurst;
    newradio::SlotAllocInfo info;
    std::tie (pktBurst, info) = m_phyBuffer.front ();

    // check if this TB has to be sent in this slot, otherwise raise an error
    NS_ASSERT_MSG (info.m_slotIdx == timingInfo.m_slotNum, "This TB is not intended for this slot");

    // send the transport block
    if (info.m_slotType == newradio::SlotAllocInfo::DATA)
    {
      usedSymbols += SlData (pktBurst, info);
    }
    else if (info.m_slotType == newradio::SlotAllocInfo::CTRL)
    {
      NS_FATAL_ERROR ("Control messages are not currently supported");
    }
    else
    {
      NS_FATAL_ERROR ("Unknown TB type");
    }

    // check if we exceeded the slot boundaries
    NS_ASSERT_MSG (usedSymbols <= m_phyMacConfig->GetSymbPerSlot (), "Exceeded number of available symbols");

    // remove the transport block from the buffer
    m_phyBuffer.pop_front ();
  }

  // convert the slot period from seconds to nanoseconds
  // TODO change GetSlotPeriod to return a TimeValue
  double slotPeriod = m_phyMacConfig->GetSymbolPeriod () * 1e3 * m_phyMacConfig->GetSymbPerSlot ();

  // update the timing information
  timingInfo = UpdateTimingInfo (timingInfo);
  Simulator::Schedule (NanoSeconds (slotPeriod), &newradioSidelinkPhy::StartSlot, this, timingInfo);
}

uint8_t
newradioSidelinkPhy::SlData (Ptr<PacketBurst> pb, newradio::SlotAllocInfo info)
{
  NS_LOG_FUNCTION (this);

  // create the tx PSD
  //TODO do we need to create a new psd at each TTI?
  std::vector<int> subChannelsForTx = SetSubChannelsForTransmission ();

  // compute the tx start time (IndexOfTheFirstSymbol * SymbolDuration)
  Time startTime = NanoSeconds (info.m_dci.m_symStart * m_phyMacConfig->GetSymbolPeriod () * 1e3);
  NS_ASSERT_MSG (startTime.GetNanoSeconds () == int64_t(info.m_dci.m_symStart * m_phyMacConfig->GetSymbolPeriod () * 1e3), "startTime was not been correctly set");

  // compute the duration of the transmission (NumberOfSymbols * SymbolDuration)
  Time duration = NanoSeconds (info.m_dci.m_numSym * m_phyMacConfig->GetSymbolPeriod () * 1e3);

  NS_ASSERT_MSG (duration.GetNanoSeconds () == int64_t(info.m_dci.m_numSym * m_phyMacConfig->GetSymbolPeriod () * 1e3), "duration was not been correctly set");

  // send the transport block
  Simulator::Schedule (startTime, &newradioSidelinkPhy::SendDataChannels, this,
                       pb,
                       duration,
                       info,
                       subChannelsForTx);

  return info.m_dci.m_numSym;
}

void
newradioSidelinkPhy::SendDataChannels (Ptr<PacketBurst> pb,
  Time duration,
  newradio::SlotAllocInfo info,
  std::vector<int> rbBitmap)
{
  // retrieve the RNTI of the device we want to communicate with and properly
  // configure the beamforming
  // NOTE: this information is contained in newradio::SlotAllocInfo.m_rnti parameter
  NS_ASSERT_MSG (m_deviceMap.find (info.m_rnti) != m_deviceMap.end (), "Device not found");
  m_sidelinkSpectrumPhy->ConfigureBeamforming (m_deviceMap.at (info.m_rnti));

  m_sidelinkSpectrumPhy->StartTxDataFrames (pb, duration, info.m_slotIdx, info.m_dci.m_mcs, info.m_dci.m_tbSize, info.m_dci.m_numSym, info.m_dci.m_rnti, info.m_rnti, rbBitmap);
}

std::vector<int>
newradioSidelinkPhy::SetSubChannelsForTransmission ()
  {
    // create the transmission mask, use all the available subchannels
    std::vector<int> subChannelsForTx (m_phyMacConfig->GetTotalNumChunk ());
    for (uint32_t i = 0; i < subChannelsForTx.size (); i++)
    {
      subChannelsForTx.at(i) = i;
    }

    // create the tx PSD
    Ptr<SpectrumValue> txPsd = newradio::newradioSpectrumValueHelper::CreateTxPowerSpectralDensity (m_phyMacConfig, m_txPower, subChannelsForTx);

    // set the tx PSD in the spectrum phy
    m_sidelinkSpectrumPhy->SetTxPowerSpectralDensity (txPsd);

    return subChannelsForTx;
  }

newradio::SfnSf
newradioSidelinkPhy::UpdateTimingInfo (newradio::SfnSf info) const
{
  NS_LOG_INFO (this);

  uint32_t nextSlot = info.m_slotNum + 1;
  uint32_t nextSf = info.m_sfNum;
  uint32_t nextFrame = info.m_frameNum;

  if (nextSlot == m_phyMacConfig->GetSlotsPerSubframe ())
  {
    nextSlot = 0;
    ++nextSf;

    if (nextSf == m_phyMacConfig->GetSubframesPerFrame ())
    {
      nextSf = 0;
      ++nextFrame;
    }
  }

  // update the newradio::SfnSf structure
  info.m_slotNum = nextSlot;
  info.m_sfNum = nextSf;
  info.m_frameNum = nextFrame;

  return info;
}

void
newradioSidelinkPhy::DoPrepareForReceptionFrom (uint16_t rnti)
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT_MSG (m_deviceMap.find (rnti) != m_deviceMap.end (), "Cannot find device with rnti " << rnti);
  m_sidelinkSpectrumPhy->ConfigureBeamforming (m_deviceMap.at (rnti));
}

void
newradioSidelinkPhy::AddDevice (uint64_t rnti, Ptr<NetDevice> dev)
{
  NS_LOG_FUNCTION (this);

  if (m_deviceMap.find (rnti) == m_deviceMap.end ())
  {
    m_deviceMap.insert (std::make_pair (rnti,dev));
  }
  else
  {
    NS_FATAL_ERROR ("Device with rnti " << rnti << " already present in the map");
  }
}

void
newradioSidelinkPhy::Receive (Ptr<Packet> p)
{
  NS_LOG_FUNCTION (this);

  // Forward the received packet to the MAC layer using the PHY SAP USER
  m_phySapUser->ReceivePhyPdu(p);
}

void
newradioSidelinkPhy::GenerateSinrReport (const SpectrumValue& sinr, uint16_t rnti, uint8_t numSym, uint32_t tbSize, uint8_t mcs)
{
  NS_LOG_FUNCTION (this << rnti << (uint32_t)numSym << tbSize << (uint32_t)mcs);

  double sinrAvg = Sum (sinr) / (sinr.GetSpectrumModel ()->GetNumBands ());
  NS_LOG_INFO ("Average SINR with dev " << rnti << " = " << 10 * std::log10 (sinrAvg));

  // forward the report to the MAC layer
  m_phySapUser->SlSinrReport (sinr, rnti, numSym, tbSize);
}

} // namespace millicar
} // namespace ns3
