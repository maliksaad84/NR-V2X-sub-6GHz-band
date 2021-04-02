#include "ns3/newradio-phy-mac-common.h"
#include "ns3/newradio-amc.h"
#include "ns3/lte-mac-sap.h"
#include "ns3/lte-radio-bearer-tag.h"
#include "newradio-sidelink-mac.h"
#include "ns3/log.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("newradioSidelinkMac");

namespace millicar {

MacSidelinkMemberPhySapUser::MacSidelinkMemberPhySapUser (Ptr<newradioSidelinkMac> mac)
  : m_mac (mac)
{

}

void
MacSidelinkMemberPhySapUser::ReceivePhyPdu (Ptr<Packet> p)
{
  m_mac->DoReceivePhyPdu (p);
}

void
MacSidelinkMemberPhySapUser::SlotIndication (newradio::SfnSf timingInfo)
{
  m_mac->DoSlotIndication (timingInfo);
}

void
MacSidelinkMemberPhySapUser::SlSinrReport (const SpectrumValue& sinr, uint16_t rnti, uint8_t numSym, uint32_t tbSize)
{
  m_mac->DoSlSinrReport (sinr, rnti, numSym, tbSize);
}

//-----------------------------------------------------------------------

RlcSidelinkMemberMacSapProvider::RlcSidelinkMemberMacSapProvider (Ptr<newradioSidelinkMac> mac)
  : m_mac (mac)
{

}

void
RlcSidelinkMemberMacSapProvider::TransmitPdu (TransmitPduParameters params)
{
  m_mac->DoTransmitPdu (params);
}

void
RlcSidelinkMemberMacSapProvider::ReportBufferStatus (ReportBufferStatusParameters params)
{
  m_mac->DoReportBufferStatus (params);
}

//-----------------------------------------------------------------------

NS_OBJECT_ENSURE_REGISTERED (newradioSidelinkMac);

TypeId
newradioSidelinkMac::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::newradioSidelinkMac")
    .SetParent<Object> ()
    .AddAttribute ("Mcs",
                   "If AMC is not used, specify a fixed MCS value.",
                   UintegerValue (0),
                   MakeUintegerAccessor (&newradioSidelinkMac::m_mcs),
                   MakeUintegerChecker<uint8_t> (0, 28))
    .AddAttribute ("UseAmc",
                   "Set to true to use adaptive modulation and coding.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&newradioSidelinkMac::m_useAmc),
                   MakeBooleanChecker ())
    .AddTraceSource ("SchedulingInfo",
                     "Information regarding the scheduling.",
                     MakeTraceSourceAccessor (&newradioSidelinkMac::m_schedulingTrace),
                     "ns3::millicar::newradioSidelinkMac::SlSchedulingTracedCallback")
  ;
  return tid;
}

newradioSidelinkMac::newradioSidelinkMac (Ptr<newradio::newradioPhyMacCommon> pmc)
{
  NS_LOG_FUNCTION (this);

  m_phyMacConfig = pmc;

  // initialize the RNTI to 0
  m_rnti = 0;

  // create the PHY SAP USER
  m_phySapUser = new MacSidelinkMemberPhySapUser (this);

  // create the MAC SAP PROVIDER
  m_macSapProvider = new RlcSidelinkMemberMacSapProvider(this);

  // create the newradio::newradioAmc instance
  m_amc = CreateObject <newradio::newradioAmc> (m_phyMacConfig);

  // initialize the scheduling patter
  std::vector<uint16_t> pattern (m_phyMacConfig->GetSlotsPerSubframe (), 0);
  m_sfAllocInfo = pattern;
}

newradioSidelinkMac::~newradioSidelinkMac (void)
{
  NS_LOG_FUNCTION (this);
}

void
newradioSidelinkMac::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  delete m_phySapUser;
  Object::DoDispose ();
}

void
newradioSidelinkMac::DoSlotIndication (newradio::SfnSf timingInfo)
{
  NS_LOG_FUNCTION (this);

  NS_ASSERT_MSG (m_rnti != 0, "First set the RNTI");
  NS_ASSERT_MSG (!m_sfAllocInfo.empty (), "First set the scheduling pattern");
  if(m_sfAllocInfo [timingInfo.m_slotNum] == m_rnti) // check if this slot is associated to the user who required it
  {
    std::list<newradio::SlotAllocInfo> allocationInfo = ScheduleResources (timingInfo);

    // associate slot alloc info and pdu
    for (auto it = allocationInfo.begin(); it != allocationInfo.end (); it++)
    {
      // retrieve the tx buffer corresponding to the assigned destination
      auto txBuffer = m_txBufferMap.find (it->m_rnti); // the destination RNTI

      if (txBuffer == m_txBufferMap.end () || txBuffer->second.empty ())
      {
        // discard the tranmission opportunity and go to the next transmission
        continue;
      }

      // otherwise, forward the packet to the PHY
      Ptr<PacketBurst> pb = CreateObject<PacketBurst> ();
      pb->AddPacket (txBuffer->second.front ().pdu);
      m_phySapProvider->AddTransportBlock (pb, *it);
      txBuffer->second.pop_front ();
    }
  }
  else if (m_sfAllocInfo[timingInfo.m_slotNum] != 0) // if the slot is assigned to another device, prepare for reception
  {
    NS_LOG_INFO ("Prepare for reception from rnti " << m_sfAllocInfo[timingInfo.m_slotNum]);
    m_phySapProvider->PrepareForReception (m_sfAllocInfo[timingInfo.m_slotNum]);
  }
  else // the slot is not assigned to any user
  {
    NS_LOG_INFO ("Empty slot");
  }

}

std::list<newradio::SlotAllocInfo>
newradioSidelinkMac::ScheduleResources (newradio::SfnSf timingInfo)
{
  std::list<newradio::SlotAllocInfo> allocationInfo; // stores all the allocation decisions

  NS_LOG_DEBUG("m_bufferStatusReportMap.size () =\t" << m_bufferStatusReportMap.size ());
  // if there are no active channels return an empty vector
  if (m_bufferStatusReportMap.size () == 0)
  {
    return allocationInfo;
  }

  // compute the total number of available symbols
  uint32_t availableSymbols = m_phyMacConfig->GetSymbPerSlot ();

  NS_LOG_DEBUG("availableSymbols =\t" << availableSymbols);

  // compute the number of available symbols per logical channel
  // NOTE the number of available symbols per LC is rounded down due to the cast
  // to int
  uint32_t availableSymbolsPerLc = availableSymbols / m_bufferStatusReportMap.size ();

  NS_LOG_DEBUG("availableSymbolsPerLc =\t" << availableSymbolsPerLc);

  // TODO start from the last served lc + 1
  auto bsrIt = m_bufferStatusReportMap.begin ();

  uint8_t symStart = 0; // indicates the next available symbol in the slot

  // serve the active logical channels with a Round Robin approach
  while (availableSymbols > 0 && m_bufferStatusReportMap.size () > 0)
  {
    uint16_t rntiDest = bsrIt->second.rnti; // the RNTI of the destination node

    uint8_t mcs = GetMcs (rntiDest); // select the MCS

    NS_LOG_DEBUG("rnti " << rntiDest << " mcs = " << uint16_t(mcs));
    // compute the number of bits for this LC
    uint32_t availableBitsPerLc = m_amc->GetTbSizeFromMcsSymbols(mcs, availableSymbolsPerLc);

    // compute the number of bits required by this LC
    uint32_t requiredBits = (bsrIt->second.txQueueSize + bsrIt->second.retxQueueSize + bsrIt->second.statusPduSize) * 8;

    // assign a number of bits which is less or equal to the available bits
    uint32_t assignedBits = 0;
    if (requiredBits <= availableBitsPerLc)
    {
      assignedBits = requiredBits;
    }
    else
    {
      assignedBits = availableBitsPerLc;
    }

    // compute the number of symbols assigned to this LC
    uint32_t assignedSymbols = m_amc->GetNumSymbolsFromTbsMcs (assignedBits, mcs);
    NS_LOG_DEBUG("assignedSymbols =\t" << assignedSymbols);

    //if (assignedSymbols <= availableSymbols) // TODO check if needed
    //{
    // create the SlotAllocInfo object
    newradio::SlotAllocInfo info;
    info.m_slotIdx = timingInfo.m_slotNum; // the TB will be sent in this slot
    info.m_rnti = rntiDest; // the RNTI of the destination node
    info.m_dci.m_rnti = m_rnti; // my RNTI
    info.m_dci.m_numSym = assignedSymbols; // the number of symbols required to tx the packet
    info.m_dci.m_symStart = symStart; // index of the first available symbol
    info.m_dci.m_mcs = mcs;
    info.m_dci.m_tbSize = assignedBits / 8; // the TB size in bytes
    info.m_slotType = newradio::SlotAllocInfo::DATA; // the TB carries data

    NS_LOG_DEBUG("info.m_dci.m_tbSize =\t" << info.m_dci.m_tbSize);

    allocationInfo.push_back (info);

    // fire the scheduling trace
    SlSchedulingCallback traceInfo;
    traceInfo.frame = timingInfo.m_frameNum;
    traceInfo.subframe = timingInfo.m_sfNum;
    traceInfo.slotNum = timingInfo.m_slotNum;
    traceInfo.symStart = symStart;
    traceInfo.numSym = assignedSymbols;
    traceInfo.mcs = mcs;
    traceInfo.tbSize = assignedBits / 8;
    traceInfo.txRnti = m_rnti;
    traceInfo.rxRnti = rntiDest;
    m_schedulingTrace (traceInfo);

    // notify the RLC
    LteMacSapUser* macSapUser = m_lcidToMacSap.find (bsrIt->second.lcid)->second;
    LteMacSapUser::TxOpportunityParameters params;
    params.bytes = assignedBits / 8;  // the number of bytes to transmit
    params.layer = 0;  // the layer of transmission (MIMO) (NOT USED)
    params.harqId = 0; // the HARQ ID (NOT USED)
    params.componentCarrierId = 0; // the component carrier id (NOT USED)
    params.rnti = rntiDest; // the C-RNTI identifying the destination
    params.lcid = bsrIt->second.lcid; // the logical channel id
    macSapUser->NotifyTxOpportunity (params);

    // update the entry in the m_bufferStatusReportMap (delete it if no
    // further resources are needed)
    bsrIt = UpdateBufferStatusReport (bsrIt->second.lcid, assignedBits / 8);

    // update the number of available symbols
    availableSymbols -= assignedSymbols;

    // update the availableSymbolsPerLc (if needed)
    if (availableSymbols < availableSymbolsPerLc)
    {
      availableSymbolsPerLc = availableSymbols;
    }

    // update index to the next available symbol
    symStart = symStart + assignedSymbols;

    // if the iterator reached the end of the map, start again
    if (bsrIt == m_bufferStatusReportMap.end ())
    {
      bsrIt = m_bufferStatusReportMap.begin ();
    }

  }
  return allocationInfo;
}

std::map<uint8_t, LteMacSapProvider::ReportBufferStatusParameters>::iterator
newradioSidelinkMac::UpdateBufferStatusReport (uint8_t lcid, uint32_t assignedBytes)
{
  // find the corresponding entry in the map
  auto bsrIt = m_bufferStatusReportMap.find (lcid);

  NS_ASSERT_MSG (bsrIt != m_bufferStatusReportMap.end (), "m_bufferStatusReportMap does not contain the required entry");

  // NOTE RLC transmits PDUs in the following priority order:
  // 1) STATUS PDUs
  // 2) retransmissions
  // 3) regular PDUs
  if (bsrIt->second.statusPduSize > assignedBytes)
  {
    bsrIt->second.statusPduSize -= assignedBytes;
    assignedBytes = 0;
  }
  else
  {
    assignedBytes -= bsrIt->second.statusPduSize;
    bsrIt->second.statusPduSize = 0;
  }

  if (bsrIt->second.retxQueueSize > assignedBytes)
  {
    bsrIt->second.retxQueueSize -= assignedBytes;
    assignedBytes = 0;
  }
  else
  {
    assignedBytes -= bsrIt->second.retxQueueSize;
    bsrIt->second.retxQueueSize = 0;
  }

  if (bsrIt->second.txQueueSize > assignedBytes)
  {
    bsrIt->second.txQueueSize -= assignedBytes;
    assignedBytes = 0;
  }
  else
  {
    assignedBytes -= bsrIt->second.txQueueSize;
    bsrIt->second.txQueueSize = 0;
  }

  // delete the entry in the map if no further resources are needed
  if (bsrIt->second.statusPduSize == 0 && bsrIt->second.retxQueueSize == 0  && bsrIt->second.txQueueSize == 0)
  {
    bsrIt = m_bufferStatusReportMap.erase (bsrIt);
  }
  else
  {
    bsrIt++;
  }

  return bsrIt;
}

void
newradioSidelinkMac::DoReportBufferStatus (LteMacSapProvider::ReportBufferStatusParameters params)
{
  NS_LOG_FUNCTION (this);

  auto bsrIt = m_bufferStatusReportMap.find (params.lcid);
  if (bsrIt != m_bufferStatusReportMap.end ())
  {
    bsrIt->second = params;
    NS_LOG_DEBUG("Update buffer status report for LCID " << uint32_t(params.lcid));
  }
  else
  {
    m_bufferStatusReportMap.insert (std::make_pair (params.lcid, params));
    NS_LOG_DEBUG("Insert buffer status report for LCID " << uint32_t(params.lcid));
  }
}

void
newradioSidelinkMac::DoTransmitPdu (LteMacSapProvider::TransmitPduParameters params)
{
  NS_LOG_FUNCTION (this);
  LteRadioBearerTag tag (params.rnti, params.lcid, params.layer);
  params.pdu->AddPacketTag (tag);

  //insert the packet at the end of the buffer
  NS_LOG_DEBUG("Add packet for RNTI " << params.rnti << " LCID " << uint32_t(params.lcid));

  auto it = m_txBufferMap.find (params.rnti);
  if (it == m_txBufferMap.end ())
  {
    std::list<LteMacSapProvider::TransmitPduParameters> txBuffer;
    txBuffer.push_back (params);
    m_txBufferMap.insert (std::make_pair (params.rnti, txBuffer));
  }
  else
  {
    it->second.push_back (params);
  }
}

void
newradioSidelinkMac::DoReceivePhyPdu (Ptr<Packet> p)
{
  NS_LOG_FUNCTION(this << p);
  LteMacSapUser::ReceivePduParameters rxPduParams;

  LteRadioBearerTag tag;
  p->PeekPacketTag (tag);

  // pick the right lcid associated to this communication. As discussed, this can be done via a dedicated SidelinkBearerTag
  rxPduParams.p = p;
  rxPduParams.rnti = tag.GetRnti ();
  rxPduParams.lcid = tag.GetLcid ();

  NS_LOG_DEBUG ("Received a packet " << rxPduParams.rnti << " " << (uint16_t)rxPduParams.lcid);

  LteMacSapUser* macSapUser = m_lcidToMacSap.find(rxPduParams.lcid)->second;
  macSapUser->ReceivePdu (rxPduParams);
}

newradioSidelinkPhySapUser*
newradioSidelinkMac::GetPhySapUser () const
{
  NS_LOG_FUNCTION (this);
  return m_phySapUser;
}

void
newradioSidelinkMac::SetPhySapProvider (newradioSidelinkPhySapProvider* sap)
{
  NS_LOG_FUNCTION (this);
  m_phySapProvider = sap;
}

LteMacSapProvider*
newradioSidelinkMac::GetMacSapProvider () const
{
  NS_LOG_FUNCTION (this);
  return m_macSapProvider;
}

void
newradioSidelinkMac::SetRnti (uint16_t rnti)
{
  m_rnti = rnti;
}

uint16_t
newradioSidelinkMac::GetRnti () const
{
  return m_rnti;
}

void
newradioSidelinkMac::SetSfAllocationInfo (std::vector<uint16_t> pattern)
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT_MSG (pattern.size () == m_phyMacConfig->GetSlotsPerSubframe (), "The number of pattern elements must be equal to the number of slots per subframe");
  m_sfAllocInfo = pattern;
}

void
newradioSidelinkMac::SetForwardUpCallback (Callback <void, Ptr<Packet> > cb)
{
  m_forwardUpCallback = cb;
}

void
newradioSidelinkMac::DoSlSinrReport (const SpectrumValue& sinr, uint16_t rnti, uint8_t numSym, uint32_t tbSize)
{
  NS_LOG_FUNCTION (this);

  // check if the m_slCqiReported already contains the CQI history for the device
  // with RNTI = rnti. If so, add the new CQI report, otherwise create a new
  // entry.
  int mcs; // the selected MCS will be stored in this variable
  if (m_slCqiReported.find (rnti) != m_slCqiReported.end () )
  {
    m_slCqiReported.at (rnti).push_back (m_amc->CreateCqiFeedbackWbTdma (sinr, numSym, tbSize, mcs));
  }
  else
  {
    std::vector<int> cqiTemp;
    cqiTemp.push_back (m_amc->CreateCqiFeedbackWbTdma (sinr, numSym, tbSize, mcs));
    m_slCqiReported.insert (std::make_pair(rnti, cqiTemp));
  }
}

uint8_t
newradioSidelinkMac::GetMcs (uint16_t rnti)
{
  NS_LOG_FUNCTION (this);

  uint8_t mcs; // the selected MCS
  if (m_useAmc)
  {
    // if AMC is used, select the MCS based on the CQI history
    if (m_slCqiReported.find (rnti) != m_slCqiReported.end ())
    {
      std::vector <int> cqi = m_slCqiReported.find (rnti)->second;
      mcs = m_amc->GetMcsFromCqi(cqi.back ());
    }
    else
    {
      mcs = 0; // if the CQI history not found for this device, use the minimum MCS value
    }
  }
  else
  {
    // if AMC is not used, use a fixed MCS value
    mcs = m_mcs;
  }
  // TODO: update the method to refresh CQI reports and delete older reports
  return mcs;
}

void
newradioSidelinkMac::AddMacSapUser (uint8_t lcid, LteMacSapUser* macSapUser)
{
  NS_LOG_FUNCTION (this);
  m_lcidToMacSap.insert(std::make_pair(lcid, macSapUser));
}

} // newradio namespace

} // ns3 namespace
