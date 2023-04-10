/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2022 Redha A. Alliche, Tiago Da Silva Barros, Ramon Aparicio-Pardo and Lucile Sassatelli
 *
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Redha A. Alliche, <alliche@i3s.unice.fr,>
 * Author: Tiago Da Silva Barros    <tiago.da-silva-barros@inria.fr>
 * Author: Ramon Aparicio-Pardo       <raparicio@i3s.unice.fr,>
 * Author: Lucile Sassatelli       <sassatelli@i3s.unice.fr,>
 *
 * Université Côte d’Azur, CNRS, I3S, Inria Sophia Antipolis, France
 *
 * Work supported in part by he  support  of  the  French  Agence  Nationale  dela Recherche (ANR), 
 * under grant ANR-19-CE-25-0001-01 (ARTIC project).
 * This  work  was  performed  using  HPC  resources  from  GENCI-IDRIS  (Grant2021-AD011012577).
 *
 */
#include "data-packet-manager.h"
#include "ns3/object.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "point-to-point-net-device.h"
#include "my-tag.h"
#include "ns3/ppp-header.h"
#include "ns3/csma-net-device.h"
#include "ns3/csma-module.h"

//#include "ns3/wifi-module.h"
#include "ns3/node-list.h"
#include "ns3/log.h"
#include <sstream>
#include <iostream>
#include <vector>
#include <fstream>



namespace ns3 {

template<typename T>
double getAverage(std::vector<T> const& v) {
  if (v.empty()) {
    return 0;
  }

  double sum = 0.0;
  for (const T &i: v) {
    sum += (double)i;
  }
  return sum / v.size();
}

template<typename T>
double getSum(std::vector<T> const& v) {
  if (v.empty()) {
    return 0;
  }
 double sum = 0.0;
  for (const T &i: v) {
    sum += (double)i;
  }
  return sum;
}

NS_LOG_COMPONENT_DEFINE ("DataPacketManager");

DataPacketManager::DataPacketManager ()
{
  PacketManager();
  NS_LOG_FUNCTION (this);
}

void
DataPacketManager::dropPacket(DataPacketManager *entity, Ptr<const Packet> packet){
  MyTag tagCopy;
  packet->PeekPacketTag(tagCopy);
  
  SentPacket packetLost;
  packetLost.start_time = tagCopy.GetStartTime();
  packetLost.type = DATA_PACKET;
  packetLost.uid = packet->GetUid();
  entity->m_lostPackets.push_back(packetLost);
}

DataPacketManager::DataPacketManager (Ptr<Node> node, vector<int> neighbors ) : PacketManager(node, neighbors)
{
  for(uint32_t i=0;i<m_node->GetNDevices();i++){
    m_node->GetDevice(i)->TraceConnectWithoutContext("MacTxDrop", MakeBoundCallback(&dropPacket, this));
  }
  NS_LOG_FUNCTION (this);
}

void 
DataPacketManager::setSmallSignalingPacketSize(uint32_t signPacketSize){
  m_signPacketSize = signPacketSize;
}

Ptr<OpenGymSpace>
DataPacketManager::getObservationSpace()
{
  NS_LOG_FUNCTION (this);
  uint32_t low = 0;
  uint32_t high = 16260; // max buffer size --> to change depending on actual value (access to defaul sim param)
  m_obs_shape = {uint32_t(1)+uint32_t(m_neighbors.size()),}; // Destination Node + (n_neighbors) interfaces for other nodes
  std::string dtype = TypeNameGet<uint32_t> ();
  Ptr<OpenGymBoxSpace> space = CreateObject<OpenGymBoxSpace> (low, high, m_obs_shape, dtype);
  return space;
}

vector<uint32_t> 
DataPacketManager::getObsShape(){
  return m_obs_shape;
}


Ptr<OpenGymSpace>
DataPacketManager::getActionSpace()
{
  NS_LOG_FUNCTION (this);
  Ptr<OpenGymDiscreteSpace> space = CreateObject<OpenGymDiscreteSpace> (m_neighbors.size()); // first dev is not p2p
  return space;
}

void 
DataPacketManager::receivePacket(Ptr<Packet> packet, Ptr<NetDevice> receivingNetDev){
  PacketManager::receivePacket(packet);
  m_receivingNetDev = receivingNetDev;
  
  //DON't NEED. THIS IS ALREADY DONE IN Poit-to-point-net-device.cc
  //if(m_arrivedAtFinalDest){
  //  m_computeStats->addE2eDelay(Simulator::Now().GetSeconds() - m_sourceTimeStamp);
  //  m_computeStats->addCost(Simulator::Now().GetSeconds() - m_sourceTimeStamp);
  //  m_computeStats->incrementOverlayPacketsArrived();
  //}
  //if(m_arrivedAtOrigin){
  //  m_computeStats->incrementOverlayPacketsInjected();
  //}
}

Ptr<OpenGymDataContainer>
DataPacketManager::getObservation()
{
  Ptr<OpenGymBoxContainer<int32_t> > box = CreateObject<OpenGymBoxContainer<int32_t> >(m_obs_shape);
  
  //Adding destination to obs
  box->AddValue(m_destination);
  

  //Preparing the config
  Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4>();
  ns3::Socket::SocketErrno sockerr;
  Ptr<Ipv4RoutingProtocol> routing = ipv4->GetRoutingProtocol( );
  Ptr<Packet> packet_test = Create<Packet>(20);

  
  //Get Neighbors for acessing their respective queues
  for (size_t i=0 ; i<m_neighbors.size(); i++){
    string string_ip= "10.2.2."+std::to_string(m_neighbors[i]+1);
    Ipv4Address ip_test(string_ip.c_str());
    m_packetIpHeader.SetDestination(ip_test);
    packet_test->AddHeader(m_packetIpHeader);
    Ptr<Ipv4Route> route = routing->RouteOutput (packet_test, m_packetIpHeader, 0, sockerr);
    Ptr<PointToPointNetDevice> dev = DynamicCast<PointToPointNetDevice>(route->GetOutputDevice());
    
    uint value;
    
    if(m_obs_bufferLength){
      value = this->getQueueLengthInBytes (m_node, dev->GetIfIndex());
    }
    
    box->AddValue(value);
  }

  return box;
}

uint32_t
DataPacketManager::getQueueLengthInBytes(Ptr<Node> node, uint32_t netDev_idx)
{
  Ptr<NetDevice> netDev = node->GetDevice (netDev_idx);
  Ptr<PointToPointNetDevice> p2p_netDev = DynamicCast<PointToPointNetDevice> (netDev);
  Ptr<Queue<Packet> > queue = p2p_netDev->GetQueue ();
  uint32_t backlog = (int) queue->GetNBytes();
  return backlog;
}

float
DataPacketManager::getReward()
{
  return 1;
}

bool
DataPacketManager::getGameOver(){
  return m_destination == m_node->GetId();
}

string
DataPacketManager::getInfo()
{
  string myInfo = PacketManager::getInfo();
  myInfo += ", Packet Lost="; //16
  while (!m_lostPackets.empty())
  {
    SentPacket lostPacket = m_lostPackets.back();
    myInfo += std::to_string(lostPacket.uid) + ";";
    m_lostPackets.pop_back();
  }
  return myInfo;
}

bool 
DataPacketManager::sendPacket(Ptr<OpenGymDataContainer> action){
  
    
  //Get discrete action
  Ptr<OpenGymDiscreteContainer> discrete = DynamicCast<OpenGymDiscreteContainer>(action);
  uint32_t fwdDev_idx = discrete->GetValue();
  
  if(m_arrivedAtFinalDest){
    // Do Not Here
  } else if (fwdDev_idx < m_neighbors.size()){
    //Setting packet Tag
    MyTag sendingTag;
    m_packet->PeekPacketTag(sendingTag);
    sendingTag.SetLastHop(m_node->GetId());
    sendingTag.SetNextHop(m_neighbors[fwdDev_idx]);
    m_packet->ReplacePacketTag(sendingTag);
    
    //Adding Headers
    m_packet->AddHeader(m_packetUdpHeader);
    string string_ip= "10.2.2."+std::to_string(m_neighbors[fwdDev_idx]+1);
    Ipv4Address ip_dest(string_ip.c_str());
    m_packetIpHeader.SetDestination(ip_dest);
    m_packet->AddHeader(m_packetIpHeader);
    
    //Discovering the output buffer based on the routing table
    //(map between overlay tunnel and netDevice)
    Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4>();
    ns3::Socket::SocketErrno sockerr;
    Ptr<Ipv4RoutingProtocol> routing = ipv4->GetRoutingProtocol( );
    Ptr<Ipv4Route> route = routing->RouteOutput (m_packet, m_packetIpHeader, 0, sockerr);
    Ptr<PointToPointNetDevice> dev = DynamicCast<PointToPointNetDevice>(route->GetOutputDevice());
    
    //Send and verify if the Packet was dropped
    SentPacket sent;
    sent.uid = m_packet->GetUid();
    sent.type = DATA_PACKET;
    sent.start_time= Simulator::Now().GetMilliSeconds();
    m_sentPackets.push_back(sent);
    dev->Send(m_packet, m_destAddr, 0x0800);
    
  } else{
    //TODO: Implement DopPacket Function
    //dropPacket(this, m_pckt);
  }
  return true;
}

void 
DataPacketManager::sendSmallSignalingPacket(){
  //Don't send if it is the source node
  if(m_source == m_node->GetId()){
    return ;
  }
  
  //Define Tag
  MyTag tagSmallSignaling;

  //Define packet size
  double packetSize=m_signPacketSize;
  
  //Setting Packet Tag
  Ptr<Packet> smallSignalingPckt = Create<Packet> (packetSize);
  tagSmallSignaling.SetSimpleValue(uint8_t(SMALL_SIGN_PACKET));
  tagSmallSignaling.SetFinalDestination(m_lastHop);
  tagSmallSignaling.SetNextHop(m_lastHop);
  tagSmallSignaling.SetLastHop(m_node->GetId());
  tagSmallSignaling.SetSource(m_node->GetId());
 
  tagSmallSignaling.SetIdValue(m_packetUid);
  tagSmallSignaling.SetTrafficValable(0);

  smallSignalingPckt->AddPacketTag(tagSmallSignaling);

  //Adding headers
  UdpHeader udp_head;
  smallSignalingPckt->AddHeader(udp_head);
  
  Ipv4Header ip_head;
  string string_ip_src= "10.2.2."+std::to_string(m_node->GetId()+1);
  Ipv4Address ip_src(string_ip_src.c_str());
  ip_head.SetSource(ip_src);
  string string_ip_dest;
  string_ip_dest= "10.2.2."+std::to_string(m_lastHop+1);
  Ipv4Address ip_dest(string_ip_dest.c_str());
  ip_head.SetDestination(ip_dest);
  ip_head.SetPayloadSize(m_signPacketSize+udp_head.GetSerializedSize());
  ip_head.SetProtocol(17);
  smallSignalingPckt->AddHeader(ip_head);
  

  //Send the sign packet
  m_receivingNetDev->Send(smallSignalingPckt, m_destAddr, 0x800);
}

}// ns3 namespace