#ifndef newradio_LTE_RRC_PROTOCOL_REAL_H
#define newradio_LTE_RRC_PROTOCOL_REAL_H

#include <stdint.h>
#include <map>

#include <ns3/ptr.h>
#include <ns3/object.h>
#include <ns3/lte-rrc-sap.h>
#include <ns3/lte-pdcp-sap.h>
#include <ns3/lte-rlc-sap.h>
#include <ns3/lte-rrc-header.h>

namespace ns3 {

class LteUeRrcSapProvider;
class LteUeRrcSapUser;
class LteEnbRrcSapProvider;
class LteUeRrc;

namespace newradio {


/**
 * Models the transmission of RRC messages from the UE to the eNB in
 * a real fashion, by creating real RRC PDUs and transmitting them
 * over Signaling Radio Bearers using radio resources allocated by the
 * LTE MAC scheduler.
 *
 */
class newradioLteUeRrcProtocolReal : public Object
{
  friend class MemberLteUeRrcSapUser<newradioLteUeRrcProtocolReal>;
  friend class LteRlcSpecificLteRlcSapUser<newradioLteUeRrcProtocolReal>;
  friend class LtePdcpSpecificLtePdcpSapUser<newradioLteUeRrcProtocolReal>;

public:
  newradioLteUeRrcProtocolReal ();
  virtual ~newradioLteUeRrcProtocolReal ();

  // inherited from Object
  virtual void DoDispose (void);
  static TypeId GetTypeId (void);

  void SetLteUeRrcSapProvider (LteUeRrcSapProvider* p);
  LteUeRrcSapUser* GetLteUeRrcSapUser ();

  void SetUeRrc (Ptr<LteUeRrc> rrc);


private:
  // methods forwarded from LteUeRrcSapUser
  void DoSetup (LteUeRrcSapUser::SetupParameters params);
  void DoSendRrcConnectionRequest (LteRrcSap::RrcConnectionRequest msg);
  void DoSendRrcConnectionSetupCompleted (LteRrcSap::RrcConnectionSetupCompleted msg);
  void DoSendRrcConnectionReconfigurationCompleted (LteRrcSap::RrcConnectionReconfigurationCompleted msg);
  void DoSendRrcConnectionReestablishmentRequest (LteRrcSap::RrcConnectionReestablishmentRequest msg);
  void DoSendRrcConnectionReestablishmentComplete (LteRrcSap::RrcConnectionReestablishmentComplete msg);
  void DoSendMeasurementReport (LteRrcSap::MeasurementReport msg);
  void DoSendNotifySecondaryCellConnected (uint16_t newradioRnti, uint16_t newradioCellId);

  void SetEnbRrcSapProvider ();
  void DoReceivePdcpPdu (Ptr<Packet> p);
  void DoReceivePdcpSdu (LtePdcpSapUser::ReceivePdcpSduParameters params);

  Ptr<LteUeRrc> m_rrc;
  uint16_t m_rnti;
  LteUeRrcSapProvider* m_ueRrcSapProvider;
  LteUeRrcSapUser* m_ueRrcSapUser;
  LteEnbRrcSapProvider* m_enbRrcSapProvider;

  LteUeRrcSapUser::SetupParameters m_setupParameters;
  LteUeRrcSapProvider::CompleteSetupParameters m_completeSetupParameters;

};


/**
 * Models the transmission of RRC messages from the UE to the eNB in
 * a real fashion, by creating real RRC PDUs and transmitting them
 * over Signaling Radio Bearers using radio resources allocated by the
 * LTE MAC scheduler.
 *
 */
class newradioLteEnbRrcProtocolReal : public Object
{
  friend class MemberLteEnbRrcSapUser<newradioLteEnbRrcProtocolReal>;
  friend class LtePdcpSpecificLtePdcpSapUser<newradioLteEnbRrcProtocolReal>;
  friend class LteRlcSpecificLteRlcSapUser<newradioLteEnbRrcProtocolReal>;
  friend class newradioRealProtocolRlcSapUser;

public:
  newradioLteEnbRrcProtocolReal ();
  virtual ~newradioLteEnbRrcProtocolReal ();

  // inherited from Object
  virtual void DoDispose (void);
  static TypeId GetTypeId (void);

  void SetLteEnbRrcSapProvider (LteEnbRrcSapProvider* p);
  LteEnbRrcSapUser* GetLteEnbRrcSapUser ();

  void SetCellId (uint16_t cellId);

  LteUeRrcSapProvider* GetUeRrcSapProvider (uint16_t rnti);
  void SetUeRrcSapProvider (uint16_t rnti, LteUeRrcSapProvider* p);

private:
  // methods forwarded from LteEnbRrcSapUser
  void DoSetupUe (uint16_t rnti, LteEnbRrcSapUser::SetupUeParameters params);
  void DoRemoveUe (uint16_t rnti);
  void DoSendSystemInformation (uint16_t cellId, LteRrcSap::SystemInformation msg);
  void SendSystemInformation (LteRrcSap::SystemInformation msg);
  void DoSendRrcConnectionSetup (uint16_t rnti, LteRrcSap::RrcConnectionSetup msg);
  void DoSendRrcConnectionReconfiguration (uint16_t rnti, LteRrcSap::RrcConnectionReconfiguration msg);
  void DoSendRrcConnectionReestablishment (uint16_t rnti, LteRrcSap::RrcConnectionReestablishment msg);
  void DoSendRrcConnectionReestablishmentReject (uint16_t rnti, LteRrcSap::RrcConnectionReestablishmentReject msg);
  void DoSendRrcConnectionRelease (uint16_t rnti, LteRrcSap::RrcConnectionRelease msg);
  void DoSendRrcConnectionReject (uint16_t rnti, LteRrcSap::RrcConnectionReject msg);
  void DoSendRrcConnectionSwitch (uint16_t rnti, LteRrcSap::RrcConnectionSwitch msg);
  void DoSendRrcConnectTonewradio (uint16_t rnti, uint16_t newradioCellId);
  Ptr<Packet> DoEncodeHandoverPreparationInformation (LteRrcSap::HandoverPreparationInfo msg);
  LteRrcSap::HandoverPreparationInfo DoDecodeHandoverPreparationInformation (Ptr<Packet> p);
  Ptr<Packet> DoEncodeHandoverCommand (LteRrcSap::RrcConnectionReconfiguration msg);
  LteRrcSap::RrcConnectionReconfiguration DoDecodeHandoverCommand (Ptr<Packet> p);

  void DoReceivePdcpSdu (LtePdcpSapUser::ReceivePdcpSduParameters params);
  void DoReceivePdcpPdu (uint16_t rnti, Ptr<Packet> p);

  uint16_t m_rnti;
  uint16_t m_cellId;
  LteEnbRrcSapProvider* m_enbRrcSapProvider;
  LteEnbRrcSapUser* m_enbRrcSapUser;
  std::map<uint16_t, LteUeRrcSapProvider*> m_enbRrcSapProviderMap;
  std::map<uint16_t, LteEnbRrcSapUser::SetupUeParameters> m_setupUeParametersMap;
  std::map<uint16_t, LteEnbRrcSapProvider::CompleteSetupUeParameters> m_completeSetupUeParametersMap;

};

///////////////////////////////////////

class newradioRealProtocolRlcSapUser : public LteRlcSapUser
{
public:
  newradioRealProtocolRlcSapUser (newradioLteEnbRrcProtocolReal* pdcp, uint16_t rnti);

  // Interface implemented from LteRlcSapUser
  virtual void ReceivePdcpPdu (Ptr<Packet> p);

private:
  newradioRealProtocolRlcSapUser ();
  newradioLteEnbRrcProtocolReal* m_pdcp;
  uint16_t m_rnti;
};


}

}


#endif // newradio_LTE_RRC_PROTOCOL_REAL_H
