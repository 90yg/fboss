/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/test/HwSwitchEnsemble.h"

#include "fboss/agent/AlpmUtils.h"
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/L2Entry.h"
#include "fboss/agent/Platform.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/hw/test/HwLinkStateToggler.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/SwitchState.h"

DEFINE_bool(
    setup_thrift,
    false,
    "Setup a thrift handler. Primarily useful for inspecting HW state,"
    "say for debugging things via a shell");

DEFINE_int32(
    thrift_port,
    5909,
    "Port for thrift server to use (use with --setup_thrift");

using namespace std::chrono_literals;

namespace facebook::fboss {

HwSwitchEnsemble::HwSwitchEnsemble(uint32_t featuresDesired)
    : featuresDesired_(featuresDesired) {}

HwSwitchEnsemble::~HwSwitchEnsemble() {
  if (thriftThread_) {
    thriftThread_->join();
  }
  if (initComplete_) { // don't touch programmedState_ unless init done
    // ALPM requires that the default routes (always required to be
    // present for ALPM) be deleted last. When we destroy the HwSwitch
    // and the contained routeTable, there is no notion of a *order* of
    // destruction.
    // So blow away all routes except the min required for ALPM
    // We are going to reset HwSwith anyways, so deleting routes does not
    // matter here.
    // Blowing away all routes means, blowing away 2 tables
    // - Route tables
    // - Interface addresses - for platforms where trapping packets to CPU is
    // done via interfaceToMe routes. So blow away routes and interface
    // addresses.
    auto noRoutesState{getProgrammedState()->clone()};
    auto routeTables = noRoutesState->getRouteTables()->modify(&noRoutesState);
    routeTables->removeRouteTable(routeTables->getRouteTable(RouterID(0)));

    auto vlans = noRoutesState->getVlans()->modify(&noRoutesState);
    for (auto& vlan : *vlans) {
      vlan->modify(&noRoutesState);
      vlan->setArpTable(std::make_shared<ArpTable>());
      vlan->setNdpTable(std::make_shared<NdpTable>());
    }

    auto newIntfMap = noRoutesState->getInterfaces()->clone();
    for (auto& interface : *newIntfMap) {
      auto newIntf = interface->clone();
      newIntf->setAddresses(Interface::Addresses{});
      newIntfMap->updateNode(newIntf);
    }
    noRoutesState->resetIntfs(newIntfMap);
    applyNewState(setupAlpmState(noRoutesState));
    // Unregister callbacks before we start destroying hwSwitch
    getHwSwitch()->unregisterCallbacks();
  }
}

HwSwitch* HwSwitchEnsemble::getHwSwitch() {
  return platform_->getHwSwitch();
}

std::shared_ptr<SwitchState> HwSwitchEnsemble::getProgrammedState() const {
  CHECK(programmedState_->isPublished());
  return programmedState_;
}

std::shared_ptr<SwitchState> HwSwitchEnsemble::applyNewState(
    std::shared_ptr<SwitchState> newState) {
  if (!newState) {
    return programmedState_;
  }
  newState->publish();
  StateDelta delta(programmedState_, newState);
  programmedState_ = getHwSwitch()->stateChanged(delta);
  if (!allowPartialStateApplication_) {
    // Assert that our desired state was applied exactly
    CHECK_EQ(newState, programmedState_);
  }
  programmedState_->publish();
  return programmedState_;
}

void HwSwitchEnsemble::applyInitialConfig(const cfg::SwitchConfig& initCfg) {
  CHECK(featuresDesired_ & HwSwitch::LINKSCAN_DESIRED)
      << "Link scan feature must be enabled for exercising "
      << "applyInitialConfig";
  linkToggler_->applyInitialConfig(
      getProgrammedState(), getPlatform(), initCfg);
  initCfgState_ = getProgrammedState();
}

void HwSwitchEnsemble::linkStateChanged(PortID port, bool up) {
  linkToggler_->linkStateChanged(port, up);
  auto hwEventObservers = hwEventObservers_.rlock();
  std::for_each(
      hwEventObservers->begin(),
      hwEventObservers->end(),
      [port, up](auto observer) { observer->linkStateChanged(port, up); });
}

void HwSwitchEnsemble::packetReceived(std::unique_ptr<RxPacket> pkt) noexcept {
  auto hwEventObservers = hwEventObservers_.rlock();
  std::for_each(
      hwEventObservers->begin(),
      hwEventObservers->end(),
      [&pkt](auto observer) { observer->packetReceived(pkt.get()); });
}

void HwSwitchEnsemble::l2LearningUpdateReceived(
    L2Entry l2Entry,
    L2EntryUpdateType l2EntryUpdateType) {
  auto hwEventObservers = hwEventObservers_.rlock();
  std::for_each(
      hwEventObservers->begin(),
      hwEventObservers->end(),
      [l2Entry, l2EntryUpdateType](auto observer) {
        observer->l2LearningUpdateReceived(l2Entry, l2EntryUpdateType);
      });
}

void HwSwitchEnsemble::addHwEventObserver(HwSwitchEventObserverIf* observer) {
  if (!hwEventObservers_->insert(observer).second) {
    throw FbossError("Observer was already added");
  }
}

void HwSwitchEnsemble::removeHwEventObserver(
    HwSwitchEventObserverIf* observer) {
  if (!hwEventObservers_->erase(observer)) {
    throw FbossError("Observer erase failed");
  }
}

bool HwSwitchEnsemble::ensureSendPacketSwitched(std::unique_ptr<TxPacket> pkt) {
  auto originalPortStats = getLatestPortStats(masterLogicalPortIds());
  bool result = getHwSwitch()->sendPacketSwitchedSync(std::move(pkt));
  return result && waitForAnyPortOutBytesIncrement(originalPortStats);
}

bool HwSwitchEnsemble::ensureSendPacketOutOfPort(
    std::unique_ptr<TxPacket> pkt,
    PortID portID,
    std::optional<uint8_t> queue) {
  auto originalPortStats = getLatestPortStats(masterLogicalPortIds());
  bool result =
      getHwSwitch()->sendPacketOutOfPortSync(std::move(pkt), portID, queue);
  return result && waitForAnyPortOutBytesIncrement(originalPortStats);
}

bool HwSwitchEnsemble::waitPortStatsCondition(
    std::function<bool(const std::map<PortID, HwPortStats>&)> conditionFn) {
  ssize_t tries = 10;
  auto newPortStats = getLatestPortStats(masterLogicalPortIds());
  while (tries--) {
    // TODO(borisb): exponential backoff!
    if (conditionFn(newPortStats)) {
      return true;
    }
    std::this_thread::sleep_for(20ms);
    newPortStats = getLatestPortStats(masterLogicalPortIds());
  }
  XLOG(DBG3) << "Awaited port stats condition was never satisfied";
  return false;
}

bool HwSwitchEnsemble::waitForAnyPortOutBytesIncrement(
    const std::map<PortID, HwPortStats>& originalPortStats) {
  auto conditionFn = [&originalPortStats](const auto& newPortStats) {
    for (const auto& portStat : originalPortStats) {
      auto newPortStatItr = newPortStats.find(portStat.first);
      if (newPortStatItr != newPortStats.end()) {
        if (*newPortStatItr->second.outBytes__ref() >
            *portStat.second.outBytes__ref()) {
          return true;
        }
      }
    }
    XLOG(DBG3) << "No port stats increased yet";
    return false;
  };
  return waitPortStatsCondition(conditionFn);
}

void HwSwitchEnsemble::setupEnsemble(
    std::unique_ptr<Platform> platform,
    std::unique_ptr<HwLinkStateToggler> linkToggler,
    std::unique_ptr<std::thread> thriftThread) {
  platform_ = std::move(platform);
  linkToggler_ = std::move(linkToggler);

  programmedState_ = getHwSwitch()->init(this).switchState;
  // HwSwitch::init() returns an unpublished programmedState_.  SwSwitch is
  // normally responsible for publishing it.  Go ahead and call publish now.
  // This will catch errors if test cases accidentally try to modify this
  // programmedState_ without first cloning it.
  programmedState_->publish();

  routingInformationBase_ = std::make_unique<rib::RoutingInformationBase>();

  // Handle ALPM state. ALPM requires that default routes be programmed
  // before any other routes. We handle that setup here. Similarly ALPM
  // requires that default routes be deleted last. That aspect is handled
  // in TearDown
  auto alpmState = setupAlpmState(programmedState_);
  if (alpmState) {
    applyNewState(alpmState);
  }

  thriftThread_ = std::move(thriftThread);

  getHwSwitch()->switchRunStateChanged(SwitchRunState::INITIALIZED);
  initComplete_ = true;
}

void HwSwitchEnsemble::revertToInitCfgState() {
  CHECK(initCfgState_);
  applyNewState(initCfgState_);
}

void HwSwitchEnsemble::gracefulExit() {
  if (thriftThread_) {
    // Join thrif thread. Thrift calls will fail post
    // warm boot exit sequence initiated below
    thriftThread_->join();
  }
  // Initiate warm boot
  folly::dynamic switchState = folly::dynamic::object;
  getHwSwitch()->unregisterCallbacks();
  switchState[kSwSwitch] = getProgrammedState()->toFollyDynamic();
  getHwSwitch()->gracefulExit(switchState);
}

} // namespace facebook::fboss
