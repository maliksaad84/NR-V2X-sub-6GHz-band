#include "newradio-vehicular-propagation-loss-model.h"
#include <ns3/log.h>
#include "ns3/mobility-model.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include <ns3/simulator.h>
#include <ns3/node.h>
#include <random>

namespace ns3 {

namespace millicar {

// ------------------------------------------------------------------------- //
NS_LOG_COMPONENT_DEFINE ("newradioVehicularPropagationLossModel");

NS_OBJECT_ENSURE_REGISTERED (newradioVehicularPropagationLossModel);

static const double g_C = 299792458.0;   // speed of light in vacuum

TypeId
newradioVehicularPropagationLossModel::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::newradioVehicularPropagationLossModel")
    .SetParent<PropagationLossModel> ()
    .AddConstructor<newradioVehicularPropagationLossModel> ()
    .AddAttribute ("Frequency",
                   "Operating frequency in Hz.",
                   DoubleValue (0.0),
                   MakeDoubleAccessor (&newradioVehicularPropagationLossModel::SetFrequency,
                                       &newradioVehicularPropagationLossModel::GetFrequency),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("MinLoss",
                   "The minimum value (dB) of the total loss, used at short ranges.",
                   DoubleValue (0.0),
                   MakeDoubleAccessor (&newradioVehicularPropagationLossModel::SetMinLoss,
                                       &newradioVehicularPropagationLossModel::GetMinLoss),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("ChannelCondition",
                   "'l' for LOS, 'n' for NLOS, 'v' for NLOSv, 'a' for all",
                   StringValue ("a"),
                   MakeStringAccessor (&newradioVehicularPropagationLossModel::m_channelConditions),
                   MakeStringChecker ())
    .AddAttribute ("Scenario",
                   "The available channel scenarios are 'V2V-Highway', 'V2V-Urban', 'Extended-V2V-Highway','Extended-V2V-Urban'",
                   StringValue ("V2V-Highway"),
                   MakeStringAccessor (&newradioVehicularPropagationLossModel::m_scenario),
                   MakeStringChecker ())
    .AddAttribute ("Shadowing",
                   "Enable shadowing effect",
                   BooleanValue (true),
                   MakeBooleanAccessor (&newradioVehicularPropagationLossModel::m_shadowingEnabled),
                   MakeBooleanChecker ())
    .AddAttribute ("Type3Vehicles",
                   "The percentage of vehicles of type 3 (i.e. trucks) in the network",
                   DoubleValue (0.0),
                   MakeDoubleAccessor (&newradioVehicularPropagationLossModel::m_percType3Vehicles),
                   MakeDoubleChecker<double> ())
  ;
  return tid;
}

newradioVehicularPropagationLossModel::newradioVehicularPropagationLossModel ()
{
  m_channelConditionMap.clear ();
  m_norVar = CreateObject<NormalRandomVariable> ();
  m_norVar->SetAttribute ("Mean", DoubleValue (0));
  m_norVar->SetAttribute ("Variance", DoubleValue (1));

  m_logNorVar = CreateObject<LogNormalRandomVariable> ();
  m_logNorVar->SetAttribute ("Mu", DoubleValue (0));

  m_uniformVar = CreateObject<UniformRandomVariable> ();
  m_uniformVar->SetAttribute ("Min", DoubleValue (0));
  m_uniformVar->SetAttribute ("Max", DoubleValue (1));

}

void
newradioVehicularPropagationLossModel::SetMinLoss (double minLoss)
{
  m_minLoss = minLoss;
}
double
newradioVehicularPropagationLossModel::GetMinLoss (void) const
{
  return m_minLoss;
}

void
newradioVehicularPropagationLossModel::SetFrequency (double freq)
{
  NS_ASSERT_MSG (freq != 0.0, "Frequency cannot be set to 0.0!");

  m_frequency = freq;
  m_lambda = g_C / m_frequency;
}

double
newradioVehicularPropagationLossModel::GetFrequency (void) const
{
  return m_frequency;
}


double
newradioVehicularPropagationLossModel::DoCalcRxPower (double txPowerDbm,
                                               Ptr<MobilityModel> a,
                                               Ptr<MobilityModel> b) const
{
  return (txPowerDbm - GetLoss (a, b));
}

double
newradioVehicularPropagationLossModel::GetLoss (Ptr<MobilityModel> deviceA, Ptr<MobilityModel> deviceB) const
{
  NS_ASSERT_MSG (m_frequency != 0.0, "Set the operating frequency first!");

  Vector aPos = deviceA->GetPosition ();
  Vector bPos = deviceB->GetPosition ();
  double x = aPos.x - bPos.x;
  double y = aPos.y - bPos.y;
  double distance2D = sqrt (x * x + y * y);
  double hA = aPos.z;
  double hB = bPos.z;

  double distance3D = deviceA->GetDistanceFrom (deviceB);

  if (distance3D < 3 * m_lambda)
    {
      NS_LOG_UNCOND ("distance not within the far field region => inaccurate propagation loss value");
    }
  if (distance3D <= 0)
    {
      return m_minLoss;
    }

  channelConditionMap_t::const_iterator it;
  it = m_channelConditionMap.find (std::make_pair (deviceA, deviceB));
  if (it == m_channelConditionMap.end ())
    {
      channelCondition condition;

      if (m_channelConditions.compare ("l") == 0 )
        {
          condition.m_channelCondition = 'l';
          NS_LOG_UNCOND (m_scenario << " scenario, channel condition is fixed to be " << condition.m_channelCondition << ", h_A=" << hA << ",h_B=" << hB);
        }
      else if (m_channelConditions.compare ("n") == 0)
        {
          condition.m_channelCondition = 'n';
          NS_LOG_UNCOND (m_scenario << " scenario, channel condition is fixed to be " << condition.m_channelCondition << ", h_A=" << hA << ",h_B=" << hB);
        }
      else if (m_channelConditions.compare ("v") == 0)
        {
          condition.m_channelCondition = 'v';
          NS_LOG_UNCOND (m_scenario << " scenario, channel condition is fixed to be " << condition.m_channelCondition << ", h_A=" << hA << ",h_B=" << hB);
        }
      else if (m_channelConditions.compare ("a") == 0)
        {
          double PRef = m_uniformVar->GetValue ();
          double probLos, probnLos, probnLosv;

          if (m_scenario == "V2V-Highway")
            {
              double a, b, c;
              a = 2.1013e-6;
              b = - 0.002;
              c = 1.0193;

              if (distance3D <= 475)
              {
                probLos = std::min(1.0, a * pow(distance3D, 2) + b * distance3D + c);
              }
              else
              {
                probLos = std::max(0.0, 0.54 - 0.001 * (distance3D - 475));
              }

              if (PRef <= probLos)
              {
                condition.m_channelCondition = 'l';
              }
              else
              {
                condition.m_channelCondition = 'v';
              }
            }
          else if (m_scenario == "V2V-Urban")
            {
              probLos = std::min(1.0, 1.05 * exp(-0.0114 * distance3D));

              if (PRef <= probLos)
              {
                condition.m_channelCondition = 'l';
              }
              else
              {
                condition.m_channelCondition = 'v';
              }
            }
          else if (m_scenario == "Extended-V2V-Highway")
            {
              // As established from TR 37.885 we  have to define
              double aLOS, bLOS, cLOS = 1;
              aLOS = 2.7e-6;
              bLOS = - 0.0025;

              probLos = std::min(1.0, std::max(0.0, aLOS * pow(distance3D, 2) + bLOS * distance3D + cLOS));

              double aNLOS, bNLOS, cNLOS = 0.015;
              aNLOS = -3.7e-7;
              bNLOS = 0.00061;

              probnLos = std::min(1.0, std::max(0.0, aNLOS * pow(distance3D, 2) + bNLOS * distance3D + cNLOS));

              if (PRef <= probLos)
                {
                  condition.m_channelCondition = 'l';
                }
              else if (PRef <= probLos + probnLos)
                {
                  condition.m_channelCondition = 'n';
                }
              else
                {
                  condition.m_channelCondition = 'v';
                }
            }
          else if (m_scenario == "Extended-V2V-Urban")
            {
              probLos = std::min(1.0, std::max(0.0, 0.8372 * exp (-0.0114*distance3D)));
              probnLosv = std::min(1.0, std::max(0.0, 1/(0.0312*distance3D) * exp(- pow(log(distance3D) - 5.0063, 2) / 2.4544)));

              if (PRef <= probLos)
                {
                  condition.m_channelCondition = 'l';
                }
              else if (PRef <= probLos + probnLosv)
                {
                  condition.m_channelCondition = 'v';
                }
              else
                {
                  condition.m_channelCondition = 'n';
                }
            }
          else
            {
              NS_FATAL_ERROR ("Unknown scenario");
            }

          NS_LOG_DEBUG (m_scenario << " scenario, 2D distance = " << distance2D << "m, Prob_LOS = " << probLos
                                    << ", Prob_REF = " << PRef << ", the channel condition is " << condition.m_channelCondition << ", h_A=" << hA << ",h_B=" << hB);
        }
      else
        {
          NS_FATAL_ERROR ("Wrong channel condition configuration");
        }

      // assign a large negative value to identify initial transmission.
      condition.m_shadowing = -1e6;

      std::pair<channelConditionMap_t::const_iterator, bool> ret;
      ret = m_channelConditionMap.insert (std::make_pair (std::make_pair (deviceA, deviceB), condition));
      m_channelConditionMap.insert (std::make_pair (std::make_pair (deviceB, deviceA), condition));
      it = ret.first;
    }

  double lossDb = 0;
  double freqGHz = m_frequency / 1e9;

  double shadowingStd = 0;
  double shadowingCorDistance = 0;

  if (m_scenario == "V2V-Highway")
  {
    // The shadowing standard deviation and decorrelation distance are
    // specified in TR 36.885 Sec. A.1.4
    shadowingStd = 3.0;
    shadowingCorDistance = 25.0;

    switch ((*it).second.m_channelCondition)
    {
      case 'l':
      {
        lossDb = 32.4 + 20 * log10 (distance3D) + 20 * log10 (freqGHz);
        break;
      }
      case 'v':
      {
        double additionalLoss = GetAdditionalNlosVLoss (distance3D, hA, hB);
        lossDb = 32.4 + 20 * log10 (distance3D) + 20 * log10 (freqGHz) + additionalLoss;
        break;
      }
      case 'n':
      {
        lossDb = 36.85 + 30 * log10 (distance3D) + 18.9 * log10 (freqGHz);
        break;
      }
      default:
        NS_FATAL_ERROR ("Programming Error.");

    }
  }
  else if (m_scenario == "V2V-Urban")
  {
    switch ((*it).second.m_channelCondition)
    {
      case 'l':
      {
        lossDb = 38.77 + 16.7 * log10 (distance3D) + 18.2 * log10 (freqGHz);

        // The shadowing standard deviation and decorrelation distance are
        // specified in TR 36.885 Sec. A.1.4
        shadowingStd = 3.0;
        shadowingCorDistance = 10.0;
        break;
      }
      case 'v':
      {
        double additionalLoss = GetAdditionalNlosVLoss (distance3D, hA, hB);

        lossDb = 38.77 + 16.7 * log10 (distance3D) + 18.2 * log10 (freqGHz) + additionalLoss;
        break;

      }
      case 'n':
      {
        lossDb = 36.85 + 30 * log10 (distance3D) + 18.9 * log10 (freqGHz);

        // The shadowing standard deviation and decorrelation distance are
        // specified in TR 36.885 Sec. A.1.4
        shadowingStd = 4.0;
        shadowingCorDistance = 10.0;
        break;
      }
      default:
        NS_FATAL_ERROR ("Programming Error.");

    }
  }
  else if (m_scenario == "Extended-V2V-Highway")
  {
    switch ((*it).second.m_channelCondition)
    {
      case 'l':
      {
        lossDb = 32.4 + 20 * log10 (distance3D) + 20 * log10 (freqGHz);

        shadowingStd = 3.0;
        // The extended model does not specify the decorrelation distance. I
        // assume the one specified by TR 36.885
        shadowingCorDistance = 25.0;
        break;
      }
      case 'v':
      {
        double additionalLoss = GetAdditionalNlosVLoss (distance3D, hA, hB);

        lossDb = 32.4 + 20 * log10 (distance3D) + 20 * log10 (freqGHz) + additionalLoss;
        break;
      }
      case 'n':
      {
        lossDb = 36.85 + 30 * log10 (distance3D) + 18.9 * log10 (freqGHz);
        shadowingStd = 4.0;
        // The extended model does not specify the decorrelation distance. I
        // assume the one specified by TR 36.885
        shadowingCorDistance = 25.0;
        break;
      }
      default:
        NS_FATAL_ERROR ("Programming Error.");
    }
  }
  else if (m_scenario == "Extended-V2V-Urban")
  {
    switch ((*it).second.m_channelCondition)
    {
      case 'l':
      {
        lossDb = 38.77 + 16.7 * log10 (distance3D) + 18.2 * log10 (freqGHz);
        shadowingStd = 3.0;
        // The extended model does not specify the decorrelation distance. I
        // assume the one specified by TR 36.885
        shadowingCorDistance = 10.0;
        break;
      }
      case 'v':
      {
        double additionalLoss = GetAdditionalNlosVLoss (distance3D, hA, hB);

        lossDb = 38.77 + 16.7 * log10 (distance3D) + 18.2 * log10 (freqGHz) + additionalLoss;
        break;
      }
      case 'n':
      {
        lossDb = 36.85 + 30 * log10 (distance3D) + 18.9 * log10 (freqGHz);
        shadowingStd = 4.0;
        // The extended model does not specify the decorrelation distance. I
        // assume the one specified by TR 36.885
        shadowingCorDistance = 10.0;
        break;
      }
      default:
        NS_FATAL_ERROR ("Programming Error.");
    }
  }
  else
    {
      NS_FATAL_ERROR ("Unknown channel scenario");
    }

  if (m_shadowingEnabled)
    {

      channelCondition cond;
      cond = (*it).second;

      //The first transmission the shadowing is initialized as -1e6,
      //we perform this if check to identify the first transmission.
      m_logNorVar->SetAttribute ("Sigma", DoubleValue (shadowingStd));

      if ((*it).second.m_shadowing < -1e5)
        {
          cond.m_shadowing = m_norVar->GetValue () * shadowingStd;
        }
      else
        {
          double deltaX = aPos.x - (*it).second.m_position.x;
          double deltaY = aPos.y - (*it).second.m_position.y;
          double disDiff = sqrt (deltaX * deltaX + deltaY * deltaY);
          double R = exp (-1 * disDiff / shadowingCorDistance);

          cond.m_shadowing = R * (*it).second.m_shadowing + sqrt (1 - R * R) * m_norVar->GetValue () * shadowingStd;
        }

      lossDb += cond.m_shadowing;
      cond.m_position = deviceA->GetPosition ();
      UpdateConditionMap (deviceA,deviceB,cond);
    }

  return std::max (lossDb, m_minLoss);
}

double
newradioVehicularPropagationLossModel::GetAdditionalNlosVLoss (double distance3D, double hA, double hB) const
{
  // From TR 37.885 v15.2.0
  // When a V2V link is in NLOSv, additional vehicle blockage loss is
  // added as follows:
  // 1. The blocker height is the vehicle height which is randomly selected
  // out of the three vehicle types according to the portion of the vehicle
  // types in the simulated scenario.
  double additionalLoss = 0;
  double blockerHeight = 0;
  double mu_a = 0;
  double sigma_a = 0;
  double randomValue = m_uniformVar->GetValue () * 3.0;
  if (randomValue < m_percType3Vehicles)
  {
    // vehicles of type 3 have height 3 meters
    blockerHeight = 3.0;
  }
  else
  {
    // vehicles of type 1 and 2 have height 1.6 meters
    blockerHeight = 1.6;
  }

  // The additional blockage loss is max {0 dB, a log-normal random variable}
  if (std::min (hA, hB) > blockerHeight)
  {
    // Case 1: Minimum antenna height value of TX and RX > Blocker height
    additionalLoss = 0;
  }
  else if (std::max (hA, hB) < blockerHeight)
  {
    // Case 2: Maximum antenna height value of TX and RX < Blocker height
    mu_a = 9.0 + std::max (0.0, 15 * log10 (distance3D) - 41.0);
    sigma_a = 4.5;
    // Pay attention to the ambiguous definition of the parameters.
    // Vehicular TR 37.885 defines mu_a and sigma_a as the mean and standard deviation of the log-normal random variable.
    // ns-3's RNG considers mu and sigma as specific parameters of the log-normal distribution, while the mean and standard deviation are evaluated separately.
    m_logNorVar->SetAttribute ("Mu", DoubleValue (log(pow(mu_a,2) / sqrt(pow(sigma_a,2) + pow(mu_a,2)))));
    m_logNorVar->SetAttribute ("Sigma", DoubleValue (sqrt(log(pow(sigma_a,2) / pow(mu_a,2) + 1))));
    additionalLoss = std::max(0.0, m_logNorVar->GetValue());
  }
  else
  {
    // Case 3: Otherwise
    mu_a = 5.0 + std::max (0.0, 15 * log10 (distance3D) - 41.0);
    sigma_a = 4.0;
    // Pay attention to the ambiguous definition of the parameters.
    // Vehicular TR 37.885 defines mu_a and sigma_a as the mean and standard deviation of the log-normal random variable.
    // ns-3's RNG considers mu and sigma as specific parameters of the log-normal distribution, while the mean and standard deviation are evaluated separately.
    m_logNorVar->SetAttribute ("Mu", DoubleValue (log(pow(mu_a,2) / sqrt(pow(sigma_a,2) + pow(mu_a,2)))));
    m_logNorVar->SetAttribute ("Sigma", DoubleValue (sqrt(log(pow(sigma_a,2) / pow(mu_a,2) + 1))));
    additionalLoss = std::max(0.0, m_logNorVar->GetValue());
  }

  return additionalLoss;
}

int64_t
newradioVehicularPropagationLossModel::DoAssignStreams (int64_t stream)
{
  return 0;
}

void
newradioVehicularPropagationLossModel::UpdateConditionMap (Ptr<MobilityModel> a, Ptr<MobilityModel> b, channelCondition cond) const
{
  m_channelConditionMap[std::make_pair (a,b)] = cond;
  m_channelConditionMap[std::make_pair (b,a)] = cond;

}

char
newradioVehicularPropagationLossModel::GetChannelCondition (Ptr<MobilityModel> a, Ptr<MobilityModel> b)
{
  channelConditionMap_t::const_iterator it;
  it = m_channelConditionMap.find (std::make_pair (a,b));
  if (it == m_channelConditionMap.end ())
    {
      NS_FATAL_ERROR ("Cannot find the link in the map");
    }
  return (*it).second.m_channelCondition;

}

std::string
newradioVehicularPropagationLossModel::GetScenario ()
{
  return m_scenario;
}

} // namespace millicar

} // namespace ns3
