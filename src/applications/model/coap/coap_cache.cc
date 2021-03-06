#include "coap_node.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CoapNode_cache");

bool CoapNode::existEntry(Ipv4Address ip, std::string url) {
  if (ip == GetAddr()) return true;

  size_t prophash = std::hash<std::string>()(Ipv4AddressToString(ip) + "" + url);

  if (!m_cache.empty()) {
    for (u_int32_t i = 0; i < m_cache.size(); ++i) {
      size_t hash = std::hash<std::string>()(Ipv4AddressToString(m_cache[i].ip) + "" + m_cache[i].url);

      if (hash == prophash) {
        return true;
      }
    }
  }
  return false;
}

int CoapNode::GetEntryIndex(size_t nodeid) {
  if (!m_cache.empty()) {
    for (u_int32_t i = 0; i < m_cache.size(); ++i) {
      size_t hash = std::hash<std::string>()(Ipv4AddressToString(m_cache[i].ip) + "" + m_cache[i].url);

      if (hash == nodeid) {
        return i;
      }
    }
  }
  return -1;
}

/*
   bool CoapNode::RefreshEntry(uint32_t index){
        if(!m_cache.empty()){
                //m_cache[index].age = Simulator::Now();
        }
        return false;
   }

 */
uint32_t CoapNode::deleteOutdated() {
  // if (m_ageTime == 0) return 0;  // No está activada la opciónd e expiración

  if (m_cache.empty()) return 0;

  uint32_t startsize =  m_cache.size();

  m_cache.erase(
    std::remove_if(
      m_cache.begin(), m_cache.end(), [](coapCacheItem item) {
    return (item.age > UINT32_MAX - 1) || (Simulator::Now().GetSeconds() > item.age);
  }
      ),
    m_cache.end()
    );

  return startsize - m_cache.size();
}

bool CoapNode::deleteEntry(size_t nodeid) {
  if (m_cache.empty()) return false;

  for (u_int32_t i = 0; i < m_cache.size(); ++i) {
    size_t hash = std::hash<std::string>()(Ipv4AddressToString(m_cache[i].ip) + "" + m_cache[i].url);
    if (hash == nodeid) {
      m_cache.erase(m_cache.begin() + i);
      return true;
    }
  }
  return false;


  // if (m_ageTime == 0) return 0;  // No está activada la opciónd e expiración
}

size_t CoapNode::getOldestEntry() {
  size_t   rindex    = 0;
  uint32_t firstTime = 0xFFFFFFFE;

  if (!m_cache.empty()) {
    for (u_int32_t i = 0; i < m_cache.size(); ++i) {
      if (m_cache[i].age < firstTime) {
        firstTime = m_cache[i].age;
        rindex    = std::hash<std::string>()(Ipv4AddressToString(m_cache[i].ip) + "" + m_cache[i].url);
      }
    }
  }
  return rindex;
}

void CoapNode::showCache() {
  /*
     if(!m_cache.empty()){
          for (u_int32_t i=0; i<m_cache.size(); ++i){
                  NS_LOG_INFO("CACHE_DUMP,"<<Simulator::Now ().GetSeconds ()<<","<< GetAddr()<<","<< std::to_string(m_cache[i].age)<<","<<Ipv4AddressToString(m_cache[i].ip)<<"/"<<m_cache[i].url);
          }
     }
     else{
     NS_LOG_INFO("CACHE_DUMP,"<<Simulator::Now ().GetSeconds ()<<","<< GetAddr()<<",EMP");
     }
   */
  deleteOutdated();
  saveCache();
  m_showCache = Simulator::Schedule(Seconds(m_cacheinterval), &CoapNode::showCache, this);
  checkCache();
}

void CoapNode::saveCache() {
  Ptr<OutputStreamWrapper> cacheStream = Create<OutputStreamWrapper>(Ipv4AddressToString(GetAddr()) + "_cache", std::ios::app);
  std::ostream *stream                 = cacheStream->GetStream();
  *stream << "CACHE_DUMP\t" << Simulator::Now().GetSeconds() << "\t" << GetAddr() << "\t" << m_cache.size() << std::endl;

  if (!m_cache.empty()) {
    for (u_int32_t i = 0; i < m_cache.size(); ++i) {
      *stream << std::to_string(m_cache[i].age) << "\t" << Ipv4AddressToString(m_cache[i].ip) << "/" << m_cache[i].url << std::endl;
    }
  }
}

void CoapNode::sendMDnsCache(Query query, Address from, uint16_t uid) {
  if ((m_stime > 0) && (checkID(uid) == PKT_CANCELED)) {
    setIDStatus(uid, PKT_OUTDATED); // xq ya no es válido, lo he cancelado
    NS_LOG_INFO(Simulator::Now().GetSeconds() << " " << GetAddr() << " CANCELED ANSWER to " << Ipv4AddressToString(InetSocketAddress::ConvertFrom(from).GetIpv4()) << " ID:" << uid);
    NS_LOG_INFO(Simulator::Now().GetSeconds() << " M_STIME:" << uid << " from " << Ipv4AddressToString(GetAddr()) << " to " << Ipv4AddressToString(InetSocketAddress::ConvertFrom(from).GetIpv4()) << " CACHE: " << 0 << " of " << m_cache.size() + 1);
    return;
  }

  if ((m_stime > 0) && (checkID(uid) == PKT_OUTDATED)) {
    NS_LOG_INFO(Simulator::Now().GetSeconds() << " M_STIME:" << uid << " from " << Ipv4AddressToString(GetAddr()) << " to " << Ipv4AddressToString(InetSocketAddress::ConvertFrom(from).GetIpv4()) << " CACHE: " << 0 << " of " << m_cache.size() + 1);
    return;
  }

  dumpIDList();

  //  89.4992 10.1.1.13 RECV 67 bytes from 10.1.1.13 DNS ANSWER ID:50668

  MDns cmdns(m_dnssocket, m_txTrace);
  cmdns.mDNSId = uid;
  cmdns.Clear();

  int servsent  = 0; // services valid to be sent
  int servtotal = 1; // Total number of services

  Ipv4Address dnsmcast(MDNS_MCAST_ADDR);
  deleteOutdated();

  if ((m_cacheopt != 0) && !m_cache.empty()) {
    for (u_int32_t i = 0; i < m_cache.size(); ++i) {
      if (m_stime == PARTIAL_SELECTIVE) {
        servtotal++;

        if ((checkServiceInDelayedResponse(uid, m_cache[i].ip, m_cache[i].url) == false) && (m_cache[i].age - (uint32_t)Simulator::Now().GetSeconds() > 0)) {
          servsent++;
          std::ostringstream oss;
          m_cache[i].ip.Print(oss);
          std::string   result  = oss.str() + "/" + m_cache[i].url;
          const char   *cresult = result.c_str();
          struct Answer rransw;

          // NS_LOG_INFO("DEBUG_ANSW from " << Ipv4AddressToString(GetAddr()) << " SEND " << cresult);
          strncpy(rransw.rdata_buffer, cresult,            MAX_MDNS_NAME_LEN);
          strncpy(rransw.name_buffer,  query.qname_buffer, MAX_MDNS_NAME_LEN);
          rransw.rrtype  = MDNS_TYPE_PTR;
          rransw.rrclass = 1; // "INternet"
          rransw.rrttl   = m_cache[i].age - (uint32_t)Simulator::Now().GetSeconds();
          rransw.rrset   = 1;
          cmdns.AddAnswer(rransw);

          // NS_LOG_INFO("MDNS_CACHE_SEND,"<<Simulator::Now ().GetSeconds ()<<","<< GetAddr()<<","<< std::to_string(m_cache[i].age)<<","<<Ipv4AddressToString(m_cache[i].ip)<<"/"<<m_cache[i].url);
        }
      } else {
        servsent++;
        std::ostringstream oss;
        m_cache[i].ip.Print(oss);
        std::string   result  = oss.str() + "/" + m_cache[i].url;
        const char   *cresult = result.c_str();
        struct Answer rransw;

        // NS_LOG_INFO("DEBUG_ANSW from " << Ipv4AddressToString(GetAddr()) << " SEND " << cresult);
        strncpy(rransw.rdata_buffer, cresult,            MAX_MDNS_NAME_LEN);
        strncpy(rransw.name_buffer,  query.qname_buffer, MAX_MDNS_NAME_LEN);
        rransw.rrtype  = MDNS_TYPE_PTR;
        rransw.rrclass = 1; // "INternet"
        rransw.rrttl   = m_cache[i].age - (uint32_t)Simulator::Now().GetSeconds();
        rransw.rrset   = 1;
        cmdns.AddAnswer(rransw);

        // NS_LOG_INFO("MDNS_CACHE_SEND,"<<Simulator::Now ().GetSeconds ()<<","<< GetAddr()<<","<< std::to_string(m_cache[i].age)<<","<<Ipv4AddressToString(m_cache[i].ip)<<"/"<<m_cache[i].url);
      }
    }
  }

  if (m_stime == PARTIAL_SELECTIVE) {
    if (checkServiceInDelayedResponse(uid, GetAddr(), "temp") == false) {
      servsent++;
      std::string   result  = Ipv4AddressToString(GetAddr()) + "/temp";
      const char   *cresult = result.c_str();
      struct Answer ownansw;
      strncpy(ownansw.rdata_buffer, cresult,            MAX_MDNS_NAME_LEN);
      strncpy(ownansw.name_buffer,  query.qname_buffer, MAX_MDNS_NAME_LEN);
      ownansw.rrtype  = MDNS_TYPE_PTR;
      ownansw.rrclass = 1; // "Internet"
      ownansw.rrttl   = m_ageTime;
      ownansw.rrset   = 1;
      cmdns.AddAnswer(ownansw);
    }
  } else {
    std::string   result  = Ipv4AddressToString(GetAddr()) + "/temp";
    const char   *cresult = result.c_str();
    struct Answer ownansw;
    strncpy(ownansw.rdata_buffer, cresult,            MAX_MDNS_NAME_LEN);
    strncpy(ownansw.name_buffer,  query.qname_buffer, MAX_MDNS_NAME_LEN);
    ownansw.rrtype  = MDNS_TYPE_PTR;
    ownansw.rrclass = 1; // "Internet"
    ownansw.rrttl   = m_ageTime;
    ownansw.rrset   = 1;
    cmdns.AddAnswer(ownansw);
    servsent++;
  }

  if (m_stime == PARTIAL_SELECTIVE) {
    NS_LOG_INFO(Simulator::Now().GetSeconds() << " M_STIME:" << uid << " from " << Ipv4AddressToString(GetAddr()) << " to " << Ipv4AddressToString(InetSocketAddress::ConvertFrom(from).GetIpv4()) << " CACHE: " << servsent << " of " << servtotal);
  }

  setIDStatus(uid, PKT_OUTDATED); // xq ya no es válido, lo he enviado una vez

  if ((m_stime == PARTIAL_SELECTIVE) && (servsent == 0)) {
    return;
  }

  NS_LOG_INFO(Simulator::Now().GetSeconds() << " " << Ipv4AddressToString(GetAddr()) << " SEND to " << Ipv4AddressToString(InetSocketAddress::ConvertFrom(from).GetIpv4()) << " MDNS CACHE ID: " << uid);

  if (m_mcast) {
    cmdns.Send(m_dnssocket, dnsmcast, m_txTrace);
  } else {
    cmdns.Send(m_dnssocket, InetSocketAddress::ConvertFrom(from).GetIpv4(), m_txTrace);
  }
}

bool CoapNode::updateEntry(Ipv4Address addr, std::string url, uint32_t maxAge) {
  size_t prophash = std::hash<std::string>()(Ipv4AddressToString(addr) + "" + url);

  if (!m_cache.empty()) {
    for (u_int32_t i = 0; i < m_cache.size(); ++i) {
      size_t hash = std::hash<std::string>()(Ipv4AddressToString(m_cache[i].ip) + "" + m_cache[i].url);

      if (hash == prophash) {
        m_cache[i].age = (uint32_t)Simulator::Now().GetSeconds() + maxAge; // This is the time where the entry is gonna be outdated
        return true;
      }
    }
  }
  return false;
}

bool CoapNode::addEntry(Ipv4Address addr, std::string url, uint32_t maxAge) {
  if (existEntry(addr, url)) return false;

  struct coapCacheItem item;
  item.age = (uint32_t)Simulator::Now().GetSeconds() + maxAge; // This is the time where the entry is gonna be outdated
  item.ip  = addr;
  item.url = url;
  m_cache.push_back(item);
  return true;
}

void CoapNode::checkCache() {
  if (m_activatePing) {
    if (!m_cache.empty()) {
      NS_LOG_INFO("START_PING");

      for (u_int32_t i = 0; i < m_cache.size(); ++i) {
        ping(m_cache[i].ip, COAP_DEFAULT_PORT);
      }
      NS_LOG_INFO("STOP_PING");
    }
  }
}
