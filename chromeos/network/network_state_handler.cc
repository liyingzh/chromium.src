// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/network_state_handler.h"

#include "base/bind.h"
#include "base/format_macros.h"
#include "base/location.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chromeos/network/device_state.h"
#include "chromeos/network/favorite_state.h"
#include "chromeos/network/managed_state.h"
#include "chromeos/network/network_event_log.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler_observer.h"
#include "chromeos/network/shill_property_handler.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace {

// Returns true if |network->type()| == |match_type|, or it matches one of the
// following special match types:
// * kMatchTypeDefault matches any network (i.e. the first instance)
// * kMatchTypeNonVirtual matches non virtual networks
// * kMatchTypeWireless matches wireless networks
// * kMatchTypeMobile matches cellular or wimax networks
bool ManagedStateMatchesType(const chromeos::ManagedState* managed,
                             const std::string& match_type) {
  const std::string& type = managed->type();
  if (match_type == chromeos::NetworkStateHandler::kMatchTypeDefault)
    return true;
  if (match_type == type)
    return true;
  if (match_type == chromeos::NetworkStateHandler::kMatchTypeNonVirtual &&
      type != flimflam::kTypeVPN) {
    return true;
  }
  if (match_type == chromeos::NetworkStateHandler::kMatchTypeWireless &&
      type != flimflam::kTypeEthernet && type != flimflam::kTypeVPN) {
    return true;
  }
  if (match_type == chromeos::NetworkStateHandler::kMatchTypeMobile &&
      (type == flimflam::kTypeCellular || type == flimflam::kTypeWimax)) {
    return true;
  }
  return false;
}

bool ConnectionStateChanged(chromeos::NetworkState* network,
                            const std::string& prev_connection_state) {
  return (network->connection_state() != prev_connection_state) &&
         (network->connection_state() != flimflam::kStateIdle ||
          !prev_connection_state.empty());
}

std::string GetManagedStateLogName(const chromeos::ManagedState* state) {
  if (!state)
    return "None";
  return base::StringPrintf("%s (%s)", state->name().c_str(),
                            state->path().c_str());
}

}  // namespace

namespace chromeos {

const char NetworkStateHandler::kMatchTypeDefault[] = "default";
const char NetworkStateHandler::kMatchTypeWireless[] = "wireless";
const char NetworkStateHandler::kMatchTypeMobile[] = "mobile";
const char NetworkStateHandler::kMatchTypeNonVirtual[] = "non-virtual";
const char NetworkStateHandler::kDefaultCheckPortalList[] =
    "ethernet,wifi,cellular";

NetworkStateHandler::NetworkStateHandler() {
}

NetworkStateHandler::~NetworkStateHandler() {
  STLDeleteContainerPointers(network_list_.begin(), network_list_.end());
  STLDeleteContainerPointers(favorite_list_.begin(), favorite_list_.end());
  STLDeleteContainerPointers(device_list_.begin(), device_list_.end());
}

void NetworkStateHandler::InitShillPropertyHandler() {
  shill_property_handler_.reset(new internal::ShillPropertyHandler(this));
  shill_property_handler_->Init();
}

// static
NetworkStateHandler* NetworkStateHandler::InitializeForTest() {
  NetworkStateHandler* handler = new NetworkStateHandler();
  handler->InitShillPropertyHandler();
  return handler;
}

void NetworkStateHandler::AddObserver(
    NetworkStateHandlerObserver* observer,
    const tracked_objects::Location& from_here) {
  observers_.AddObserver(observer);
  network_event_log::internal::AddEntry(
      from_here.file_name(), from_here.line_number(),
      network_event_log::LOG_LEVEL_DEBUG,
      "NetworkStateHandler::AddObserver", "");
}

void NetworkStateHandler::RemoveObserver(
    NetworkStateHandlerObserver* observer,
    const tracked_objects::Location& from_here) {
  observers_.RemoveObserver(observer);
  network_event_log::internal::AddEntry(
      from_here.file_name(), from_here.line_number(),
      network_event_log::LOG_LEVEL_DEBUG,
      "NetworkStateHandler::RemoveObserver", "");
}

void NetworkStateHandler::UpdateManagerProperties() {
  NET_LOG_USER("UpdateManagerProperties", "");
  shill_property_handler_->UpdateManagerProperties();
}

NetworkStateHandler::TechnologyState NetworkStateHandler::GetTechnologyState(
    const std::string& type) const {
  std::string technology = GetTechnologyForType(type);
  TechnologyState state;
  if (shill_property_handler_->IsTechnologyEnabled(technology))
    state = TECHNOLOGY_ENABLED;
  else if (shill_property_handler_->IsTechnologyEnabling(technology))
    state = TECHNOLOGY_ENABLING;
  else if (shill_property_handler_->IsTechnologyUninitialized(technology))
    state = TECHNOLOGY_UNINITIALIZED;
  else if (shill_property_handler_->IsTechnologyAvailable(technology))
    state = TECHNOLOGY_AVAILABLE;
  else
    state = TECHNOLOGY_UNAVAILABLE;
  VLOG(2) << "GetTechnologyState: " << type << " = " << state;
  return state;
}

void NetworkStateHandler::SetTechnologyEnabled(
    const std::string& type,
    bool enabled,
    const network_handler::ErrorCallback& error_callback) {
  std::string technology = GetTechnologyForType(type);
  NET_LOG_USER("SetTechnologyEnabled",
               base::StringPrintf("%s:%d", technology.c_str(), enabled));
  shill_property_handler_->SetTechnologyEnabled(
      technology, enabled, error_callback);
  // Signal Technology state changed -> ENABLING
  NotifyManagerPropertyChanged();
}

const DeviceState* NetworkStateHandler::GetDeviceState(
    const std::string& device_path) const {
  return GetModifiableDeviceState(device_path);
}

const DeviceState* NetworkStateHandler::GetDeviceStateByType(
    const std::string& type) const {
  for (ManagedStateList::const_iterator iter = device_list_.begin();
       iter != device_list_.end(); ++iter) {
    ManagedState* device = *iter;
    if (ManagedStateMatchesType(device, type))
      return device->AsDeviceState();
  }
  return NULL;
}

bool NetworkStateHandler::GetScanningByType(const std::string& type) const {
  for (ManagedStateList::const_iterator iter = device_list_.begin();
       iter != device_list_.end(); ++iter) {
    const DeviceState* device = (*iter)->AsDeviceState();
    DCHECK(device);
    if (ManagedStateMatchesType(device, type) && device->scanning())
      return true;
  }
  return false;
}

const NetworkState* NetworkStateHandler::GetNetworkState(
    const std::string& service_path) const {
  return GetModifiableNetworkState(service_path);
}

const NetworkState* NetworkStateHandler::DefaultNetwork() const {
  if (network_list_.empty())
    return NULL;
  const NetworkState* network = network_list_.front()->AsNetworkState();
  DCHECK(network);
  if (!network->IsConnectedState())
    return NULL;
  return network;
}

const NetworkState* NetworkStateHandler::ConnectedNetworkByType(
    const std::string& type) const {
  for (ManagedStateList::const_iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    const NetworkState* network = (*iter)->AsNetworkState();
    DCHECK(network);
    if (!network->IsConnectedState())
      break;  // Connected networks are listed first.
    if (ManagedStateMatchesType(network, type))
      return network;
  }
  return NULL;
}

const NetworkState* NetworkStateHandler::ConnectingNetworkByType(
    const std::string& type) const {
  for (ManagedStateList::const_iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    const NetworkState* network = (*iter)->AsNetworkState();
    DCHECK(network);
    if (network->IsConnectedState())
      continue;
    if (!network->IsConnectingState())
      break;  // Connected and connecting networks are listed first.
    if (ManagedStateMatchesType(network, type))
      return network;
  }
  return NULL;
}

const NetworkState* NetworkStateHandler::FirstNetworkByType(
    const std::string& type) const {
  for (ManagedStateList::const_iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    const NetworkState* network = (*iter)->AsNetworkState();
    DCHECK(network);
    if (ManagedStateMatchesType(network, type))
      return network;
  }
  return NULL;
}

std::string NetworkStateHandler::HardwareAddressForType(
    const std::string& type) const {
  std::string result;
  const NetworkState* network = ConnectedNetworkByType(type);
  if (network) {
    const DeviceState* device = GetDeviceState(network->device_path());
    if (device)
      result = device->mac_address();
  }
  StringToUpperASCII(&result);
  return result;
}

std::string NetworkStateHandler::FormattedHardwareAddressForType(
    const std::string& type) const {
  std::string address = HardwareAddressForType(type);
  if (address.size() % 2 != 0)
    return address;
  std::string result;
  for (size_t i = 0; i < address.size(); ++i) {
    if ((i != 0) && (i % 2 == 0))
      result.push_back(':');
    result.push_back(address[i]);
  }
  return result;
}

void NetworkStateHandler::GetNetworkList(NetworkStateList* list) const {
  DCHECK(list);
  NetworkStateList result;
  list->clear();
  for (ManagedStateList::const_iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    const NetworkState* network = (*iter)->AsNetworkState();
    DCHECK(network);
    list->push_back(network);
  }
}

void NetworkStateHandler::GetFavoriteList(FavoriteStateList* list) const {
  DCHECK(list);
  FavoriteStateList result;
  list->clear();
  for (ManagedStateList::const_iterator iter = favorite_list_.begin();
       iter != favorite_list_.end(); ++iter) {
    const FavoriteState* favorite = (*iter)->AsFavoriteState();
    DCHECK(favorite);
    if (favorite->is_favorite())
      list->push_back(favorite);
  }
}

const FavoriteState* NetworkStateHandler::GetFavoriteState(
    const std::string& service_path) const {
  ManagedState* managed =
      GetModifiableManagedState(&favorite_list_, service_path);
  if (!managed)
    return NULL;
  return managed->AsFavoriteState();
}

void NetworkStateHandler::RequestScan() const {
  NET_LOG_USER("RequestScan", "");
  shill_property_handler_->RequestScan();
}

void NetworkStateHandler::WaitForScan(const std::string& type,
                                      const base::Closure& callback) {
  scan_complete_callbacks_[type].push_back(callback);
  if (!GetScanningByType(type))
    RequestScan();
}

void NetworkStateHandler::ConnectToBestWifiNetwork() {
  NET_LOG_USER("ConnectToBestWifiNetwork", "");
  WaitForScan(flimflam::kTypeWifi,
              base::Bind(&internal::ShillPropertyHandler::ConnectToBestServices,
                         shill_property_handler_->AsWeakPtr()));
}

bool NetworkStateHandler::RequestUpdateForNetwork(
    const std::string& service_path) {
  NetworkState* network = GetModifiableNetworkState(service_path);
  if (!network)
    return false;  // Only request an update for known networks.
  network->set_update_requested(true);
  NET_LOG_EVENT("RequestUpdate", service_path);
  shill_property_handler_->RequestProperties(
      ManagedState::MANAGED_TYPE_NETWORK, service_path);
  return true;
}

void NetworkStateHandler::RequestUpdateForAllNetworks() {
  NET_LOG_EVENT("RequestUpdateForAllNetworks", "");
  for (ManagedStateList::iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    ManagedState* network = *iter;
    network->set_update_requested(true);
    shill_property_handler_->RequestProperties(
        ManagedState::MANAGED_TYPE_NETWORK, network->path());
  }
}

void NetworkStateHandler::SetConnectingNetwork(
    const std::string& service_path) {
  connecting_network_ = service_path;
  const NetworkState* network = GetNetworkState(service_path);
  if (network)
    NET_LOG_EVENT("SetConnectingNetwork", GetManagedStateLogName(network));
  else
    NET_LOG_ERROR("SetConnectingNetwork to unknown network", service_path);
}

void NetworkStateHandler::SetCheckPortalList(
    const std::string& check_portal_list) {
  NET_LOG_EVENT("SetCheckPortalList", check_portal_list);
  shill_property_handler_->SetCheckPortalList(check_portal_list);
}

void NetworkStateHandler::GetNetworkStatePropertiesForTest(
    base::DictionaryValue* dictionary) const {
  for (ManagedStateList::const_iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    base::DictionaryValue* network_dict = new base::DictionaryValue;
    (*iter)->AsNetworkState()->GetProperties(network_dict);
    dictionary->SetWithoutPathExpansion((*iter)->path(), network_dict);
  }
}

//------------------------------------------------------------------------------
// ShillPropertyHandler::Delegate overrides

void NetworkStateHandler::UpdateManagedList(ManagedState::ManagedType type,
                                            const base::ListValue& entries) {
  ManagedStateList* managed_list = GetManagedList(type);
  NET_LOG_DEBUG(base::StringPrintf("UpdateManagedList:%d", type),
                base::StringPrintf("%"PRIuS, entries.GetSize()));
  // Create a map of existing entries.
  std::map<std::string, ManagedState*> managed_map;
  for (ManagedStateList::iterator iter = managed_list->begin();
       iter != managed_list->end(); ++iter) {
    ManagedState* managed = *iter;
    managed_map[managed->path()] = managed;
  }
  // Clear the list (pointers are owned by managed_map).
  managed_list->clear();
  // Updates managed_list and request updates for new entries.
  for (base::ListValue::const_iterator iter = entries.begin();
       iter != entries.end(); ++iter) {
    std::string path;
    (*iter)->GetAsString(&path);
    DCHECK(!path.empty());
    std::map<std::string, ManagedState*>::iterator found =
        managed_map.find(path);
    ManagedState* managed;
    if (found == managed_map.end()) {
      managed = ManagedState::Create(type, path);
      managed_list->push_back(managed);
    } else {
      managed = found->second;
      managed_list->push_back(managed);
      managed_map.erase(found);
    }
  }
  // Delete any remaning entries in managed_map.
  STLDeleteContainerPairSecondPointers(managed_map.begin(), managed_map.end());
}

void NetworkStateHandler::ProfileListChanged() {
  NET_LOG_EVENT("ProfileListChanged", "Re-Requesting Network Properties");
  for (ManagedStateList::iterator iter = network_list_.begin();
       iter != network_list_.end(); ++iter) {
    shill_property_handler_->RequestProperties(
        ManagedState::MANAGED_TYPE_NETWORK, (*iter)->path());
  }
}

void NetworkStateHandler::UpdateManagedStateProperties(
    ManagedState::ManagedType type,
    const std::string& path,
    const base::DictionaryValue& properties) {
  ManagedState* managed = GetModifiableManagedState(GetManagedList(type), path);
  if (!managed) {
    LOG(ERROR) << "GetPropertiesCallback: " << path << " Not found!";
    return;
  }
  bool network_property_updated = false;
  std::string prev_connection_state;
  if (type == ManagedState::MANAGED_TYPE_NETWORK)
    prev_connection_state = managed->AsNetworkState()->connection_state();
  for (base::DictionaryValue::Iterator iter(properties);
       !iter.IsAtEnd(); iter.Advance()) {
    if (type == ManagedState::MANAGED_TYPE_NETWORK) {
      if (managed->PropertyChanged(iter.key(), iter.value()))
        network_property_updated = true;
    } else {
      managed->PropertyChanged(iter.key(), iter.value());
    }
  }
  managed->InitialPropertiesReceived();
  NET_LOG_DEBUG("PropertiesReceived", GetManagedStateLogName(managed));
  // Notify observers.
  if (network_property_updated || managed->update_requested()) {
    NetworkState* network = managed->AsNetworkState();
    DCHECK(network);
    // Signal connection state changed after all properties have been updated.
    if (ConnectionStateChanged(network, prev_connection_state)) {
      OnNetworkConnectionStateChanged(network);
      // If the network did not already have a profile entry, refresh favorites.
      if (network->profile_path().empty())
        UpdateManagerProperties();
    }
    NetworkPropertiesUpdated(network);
  }
  managed->set_update_requested(false);
}

void NetworkStateHandler::UpdateNetworkServiceProperty(
    const std::string& service_path,
    const std::string& key,
    const base::Value& value) {
  // Update any associated FavoriteState.
  ManagedState* favorite =
      GetModifiableManagedState(&favorite_list_, service_path);
  if (favorite)
    favorite->PropertyChanged(key, value);

  // Update the NetworkState.
  NetworkState* network = GetModifiableNetworkState(service_path);
  if (!network)
    return;
  std::string prev_connection_state = network->connection_state();
  if (!network->PropertyChanged(key, value))
    return;

  if (key == flimflam::kStateProperty) {
    if (ConnectionStateChanged(network, prev_connection_state))
      OnNetworkConnectionStateChanged(network);
  } else {
    if (network->path() == default_network_path_ &&
        key != flimflam::kSignalStrengthProperty) {
      // WiFi signal strength updates are too noisy, so don't
      // trigger default network updates for those changes.
      OnDefaultNetworkChanged();
    }
    std::string detail = network->name() + "." + key;
    detail += " = " + network_event_log::ValueAsString(value);
    network_event_log::LogLevel log_level = network_event_log::LOG_LEVEL_EVENT;
    if (key == flimflam::kErrorProperty || key == shill::kErrorDetailsProperty)
      log_level = network_event_log::LOG_LEVEL_ERROR;
    else if (key == flimflam::kSignalStrengthProperty ||
             key == shill::kWifiFrequencyListProperty)
      log_level = network_event_log::LOG_LEVEL_DEBUG;
    NET_LOG_LEVEL(log_level, "NetworkPropertyUpdated", detail);
  }
  NetworkPropertiesUpdated(network);
}

void NetworkStateHandler::UpdateDeviceProperty(const std::string& device_path,
                                               const std::string& key,
                                               const base::Value& value) {
  DeviceState* device = GetModifiableDeviceState(device_path);
  if (!device)
    return;
  if (!device->PropertyChanged(key, value))
    return;

  std::string detail = device->name() + "." + key;
  detail += " = " + network_event_log::ValueAsString(value);
  NET_LOG_EVENT("DevicePropertyUpdated", detail);

  FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                    DeviceListChanged());

  if (key == flimflam::kScanningProperty && device->scanning() == false)
    ScanCompleted(device->type());
}

void NetworkStateHandler::CheckPortalListChanged(
    const std::string& check_portal_list) {
  check_portal_list_ = check_portal_list;
}

void NetworkStateHandler::NotifyManagerPropertyChanged() {
  NET_LOG_DEBUG("NotifyManagerPropertyChanged", "");
  FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                    NetworkManagerChanged());
}

void NetworkStateHandler::ManagedStateListChanged(
    ManagedState::ManagedType type) {
  if (type == ManagedState::MANAGED_TYPE_NETWORK) {
    // Notify observers that the list of networks has changed.
    NET_LOG_EVENT("NetworkListChanged",
                  base::StringPrintf("Size:%"PRIuS, network_list_.size()));
    FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                      NetworkListChanged());
    // The list order may have changed, so check if the default network changed.
    if (CheckDefaultNetworkChanged())
      OnDefaultNetworkChanged();
  } else if (type == ManagedState::MANAGED_TYPE_FAVORITE) {
    NET_LOG_DEBUG("FavoriteListChanged",
                  base::StringPrintf("Size:%"PRIuS, favorite_list_.size()));
    FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                      NetworkListChanged());
  } else if (type == ManagedState::MANAGED_TYPE_DEVICE) {
    NET_LOG_DEBUG("DeviceListChanged",
                  base::StringPrintf("Size:%"PRIuS, device_list_.size()));
    FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                      DeviceListChanged());
  } else {
    NOTREACHED();
  }
}

//------------------------------------------------------------------------------
// Private methods

DeviceState* NetworkStateHandler::GetModifiableDeviceState(
    const std::string& device_path) const {
  ManagedState* managed = GetModifiableManagedState(&device_list_, device_path);
  if (!managed)
    return NULL;
  return managed->AsDeviceState();
}

NetworkState* NetworkStateHandler::GetModifiableNetworkState(
    const std::string& service_path) const {
  ManagedState* managed =
      GetModifiableManagedState(&network_list_, service_path);
  if (!managed)
    return NULL;
  return managed->AsNetworkState();
}

ManagedState* NetworkStateHandler::GetModifiableManagedState(
    const ManagedStateList* managed_list,
    const std::string& path) const {
  for (ManagedStateList::const_iterator iter = managed_list->begin();
       iter != managed_list->end(); ++iter) {
    ManagedState* managed = *iter;
    if (managed->path() == path)
      return managed;
  }
  return NULL;
}

NetworkStateHandler::ManagedStateList* NetworkStateHandler::GetManagedList(
    ManagedState::ManagedType type) {
  switch (type) {
    case ManagedState::MANAGED_TYPE_NETWORK:
      return &network_list_;
    case ManagedState::MANAGED_TYPE_FAVORITE:
      return &favorite_list_;
    case ManagedState::MANAGED_TYPE_DEVICE:
      return &device_list_;
  }
  NOTREACHED();
  return NULL;
}

void NetworkStateHandler::OnNetworkConnectionStateChanged(
    NetworkState* network) {
  DCHECK(network);
  NET_LOG_EVENT("NetworkConnectionStateChanged", base::StringPrintf(
      "%s:%s", GetManagedStateLogName(network).c_str(),
      network->connection_state().c_str()));
  FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                    NetworkConnectionStateChanged(network));
  if (CheckDefaultNetworkChanged() || network->path() == default_network_path_)
    OnDefaultNetworkChanged();
}

bool NetworkStateHandler::CheckDefaultNetworkChanged() {
  std::string new_default_network_path;
  const NetworkState* new_default_network = DefaultNetwork();
  if (new_default_network)
    new_default_network_path = new_default_network->path();
  if (new_default_network_path == default_network_path_)
    return false;
  default_network_path_ = new_default_network_path;
  return true;
}

void NetworkStateHandler::OnDefaultNetworkChanged() {
  const NetworkState* default_network = DefaultNetwork();
  NET_LOG_EVENT("DefaultNetworkChanged",
                GetManagedStateLogName(default_network));
  FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                    DefaultNetworkChanged(default_network));
}

void NetworkStateHandler::NetworkPropertiesUpdated(
    const NetworkState* network) {
  FOR_EACH_OBSERVER(NetworkStateHandlerObserver, observers_,
                    NetworkPropertiesUpdated(network));
  // If |connecting_network_| transitions to a non-idle, non-connecting state,
  // clear it *after* signalling observers.
  if (network->path() == connecting_network_ &&
      !network->IsConnectingState() &&
      network->connection_state() != flimflam::kStateIdle) {
    connecting_network_.clear();
    NET_LOG_EVENT("ClearConnectingNetwork", base::StringPrintf(
        "%s:%s", GetManagedStateLogName(network).c_str(),
        network->connection_state().c_str()));
  }
}

void NetworkStateHandler::ScanCompleted(const std::string& type) {
  size_t num_callbacks = scan_complete_callbacks_.count(type);
  NET_LOG_EVENT("ScanCompleted",
                base::StringPrintf("%s:%"PRIuS, type.c_str(), num_callbacks));
  if (num_callbacks == 0)
    return;
  ScanCallbackList& callback_list = scan_complete_callbacks_[type];
  for (ScanCallbackList::iterator iter = callback_list.begin();
       iter != callback_list.end(); ++iter) {
    (*iter).Run();
  }
  scan_complete_callbacks_.erase(type);
}

std::string NetworkStateHandler::GetTechnologyForType(
    const std::string& type) const {
  if (type == kMatchTypeMobile) {
    if (shill_property_handler_->IsTechnologyAvailable(flimflam::kTypeWimax))
      return flimflam::kTypeWimax;
    else
      return flimflam::kTypeCellular;
  }
  if (type == kMatchTypeDefault || type == kMatchTypeNonVirtual ||
      type == kMatchTypeWireless) {
    NOTREACHED();
    return flimflam::kTypeWifi;
  }
  return type;
}

}  // namespace chromeos
