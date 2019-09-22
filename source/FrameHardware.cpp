#pragma once

#include <algorithm>

#include "base/vector.h"
#include "base/Ptr.h"

#include "Config.h"
#include "FrameCell.h"
#include "Manager.h"
#include "DishWorld.h"
#include "FrameHardware.h"

FrameHardware::FrameHardware(
    FrameCell &cell_,
    const size_t facing_,
    emp::Random &local_rng_,
    const Config &cfg_,
    const Config::inst_lib_t &inst_lib,
    const Config::event_lib_t &event_lib
  ) : cell(cell_)
    , local_rng(local_rng_)
    , cfg(cfg_)
    , facing(facing_)
    , inbox_active(true)
    , stockpile_reserve(0.0)
    , stockpile_reserve_fresh(0)
    , reproduction_reserve(0.0)
    , reproduction_reserve_fresh(0)
    , cpu(inst_lib, event_lib, &local_rng_)
    , membrane(local_rng_)
  {
    cpu.SetTrait(this);
  }

FrameCell& FrameHardware::Cell() { return cell; }

void FrameHardware::Reset() {
  inbox_active = true;

  stockpile_reserve = 0.0;
  stockpile_reserve_fresh = 0;

  reproduction_reserve = 0.0;
  reproduction_reserve_fresh = 0;

  cpu.ResetHardware();
  cpu.ResetProgram();
  membrane.Clear();
  membrane_tags.clear();
  emp_assert(!cpu.GetProgram().GetSize());
}

double FrameHardware::CheckStockpileReserve() const {
  return stockpile_reserve;
}

void FrameHardware::SetStockpileReserve(const double amt, const size_t dur) {
  stockpile_reserve = std::max(0.0,amt);
  stockpile_reserve_fresh = dur;
}

void FrameHardware::TryClearStockpileReserve() {
  if (stockpile_reserve_fresh) --stockpile_reserve_fresh;
  if (!stockpile_reserve_fresh) stockpile_reserve = 0.0;
}

double FrameHardware::CheckReproductionReserve() const {
  return reproduction_reserve;
}

void FrameHardware::SetReproductionReserve(const double amt, const size_t dur) {
  reproduction_reserve = std::max(0.0,amt);
  reproduction_reserve_fresh = dur;
}

void FrameHardware::TryClearReproductionReserve() {
  if (reproduction_reserve_fresh) --reproduction_reserve_fresh;
  if (!reproduction_reserve_fresh) reproduction_reserve = 0.0;
}

void FrameHardware::DispatchEnvTriggers(const size_t update){

  // need at least 12 cpus available
  emp_assert(cfg.HW_MAX_CORES() > 12);
  // ... so make sure at least 8 are unoccupied
  cpu.SetMaxCores(cfg.HW_MAX_CORES() - 12);
  cpu.SetMaxCores(cfg.HW_MAX_CORES());


  static emp::Random rng(cfg.SEED()+1);
  static emp::vector<Config::tag_t> pro_trigger_tags;
  static emp::vector<Config::tag_t> anti_trigger_tags;

  size_t i = 0;

  // cell child trigger
  if(i >= pro_trigger_tags.size()) {
    pro_trigger_tags.emplace_back(rng);
    auto copy = pro_trigger_tags[i];
    anti_trigger_tags.emplace_back(copy.Toggle());
  }
  if (cfg.CHANNELS_VISIBLE()) {
    if (IsCellChild()) {
      cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);
    } else {
      // cpu.TriggerEvent("EnvTrigger", anti_trigger_tags[i]);
    }
  }

  ++i;

  // cell parent trigger
  // note: mutually exclusive with cell child trigger
  if(i >= pro_trigger_tags.size()) {
    pro_trigger_tags.emplace_back(rng);
    auto copy = pro_trigger_tags[i];
    anti_trigger_tags.emplace_back(copy.Toggle());
  }
  if (cfg.CHANNELS_VISIBLE()) {
    if (IsCellParent()) {
      cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);
    } else {
      // cpu.TriggerEvent("EnvTrigger", anti_trigger_tags[i]);
    }
  }

  ++i;

  // negative resource trigger
  if(i >= pro_trigger_tags.size()) {
    pro_trigger_tags.emplace_back(rng);
    auto copy = pro_trigger_tags[i];
    anti_trigger_tags.emplace_back(copy.Toggle());
  }
  if (Cell().Man().Stockpile(Cell().GetPos()).QueryResource() < 0) {
    cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);
  } else {
    // cpu.TriggerEvent("EnvTrigger", anti_trigger_tags[i]);
  }

  ++i;

  // expiration trigger
  if (i >= pro_trigger_tags.size()) {
    pro_trigger_tags.emplace_back(rng);
    auto copy = pro_trigger_tags[i];
    anti_trigger_tags.emplace_back(copy.Toggle());
  }

  if (
    Cell().Man().Channel(Cell().GetPos()).IsExpired(0)
    > cfg.EXP_GRACE_PERIOD()
  ) {
    cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);
  } else {
    // cpu.TriggerEvent("EnvTrigger", anti_trigger_tags[i]);
  }

  ++i;

  // harvest withdrawal trigger
  if (i >= pro_trigger_tags.size()) {
    pro_trigger_tags.emplace_back(rng);
    auto copy = pro_trigger_tags[i];
    anti_trigger_tags.emplace_back(copy.Toggle());
  }

  for (size_t lev = 0; lev < cfg.NLEV(); ++lev) {
    if (Cell().Man().Stockpile(Cell().GetPos()).QueryHarvestWithdrawals(lev)) {
      cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);
      break;
    }
  }

  // clean up
  for (size_t lev = 0; lev < cfg.NLEV(); ++lev) {
    Cell().Man().Stockpile(Cell().GetPos()).ResetHarvestWithdrawals(lev);
  }

  ++i;


  // channel match triggers
  for(size_t lev = 0; lev < cfg.NLEV(); ++lev) {
    if(i >= pro_trigger_tags.size()) {
      pro_trigger_tags.emplace_back(rng);
      auto copy = pro_trigger_tags[i];
      anti_trigger_tags.emplace_back(copy.Toggle());
    }
    if (cfg.CHANNELS_VISIBLE()) {
      if (IsChannelMate(lev)) {
        cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);
      } else {
        cpu.TriggerEvent("EnvTrigger", anti_trigger_tags[i]);
      }
    }
    ++i;
  }

  // propagule child trigger
  if(i >= pro_trigger_tags.size()) {
    pro_trigger_tags.emplace_back(rng);
    auto copy = pro_trigger_tags[i];
    anti_trigger_tags.emplace_back(copy.Toggle());
  }
  if (cfg.CHANNELS_VISIBLE()) {
    if (IsPropaguleChild()) {
      cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);
    } else {
      // cpu.TriggerEvent("EnvTrigger", anti_trigger_tags[i]);
    }
  }

  ++i;

  // propagule parent trigger
  // note: mutually exclusive with propagule child trigger
  if(i >= pro_trigger_tags.size()) {
    pro_trigger_tags.emplace_back(rng);
    auto copy = pro_trigger_tags[i];
    anti_trigger_tags.emplace_back(copy.Toggle());
  }
  if (cfg.CHANNELS_VISIBLE()) {
    if (IsPropaguleParent()) {
      cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);
    } else {
      // cpu.TriggerEvent("EnvTrigger", anti_trigger_tags[i]);
    }
  }

  ++i;

  // neighbor is dead or live?
  if(i >= pro_trigger_tags.size()) {
    pro_trigger_tags.emplace_back(rng);
    auto copy = pro_trigger_tags[i];
    anti_trigger_tags.emplace_back(copy.Toggle());
  }
  // if (cfg.CHANNELS_VISIBLE()) {
    if (IsLive()) {
      cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);
    } else {
      cpu.TriggerEvent("EnvTrigger", anti_trigger_tags[i]);
    }
  // }

  ++i;

  // neighbor has more resource or less?
  if(i >= pro_trigger_tags.size()) {
    pro_trigger_tags.emplace_back(rng);
    auto copy = pro_trigger_tags[i];
    anti_trigger_tags.emplace_back(copy.Toggle());
  }
  // if (cfg.CHANNELS_VISIBLE()) {
    if (IsPoorerThan() && IsLive()) {
      cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);
    } else if (IsLive()) {
      cpu.TriggerEvent("EnvTrigger", anti_trigger_tags[i]);
    }
  // }

  ++i;

  // neighbor cell age older or younger?
  if(i >= pro_trigger_tags.size()) {
    pro_trigger_tags.emplace_back(rng);
    auto copy = pro_trigger_tags[i];
    anti_trigger_tags.emplace_back(copy.Toggle());
  }
  // if (cfg.CHANNELS_VISIBLE()) {
    if (IsOlderThan() && IsLive()) {
      cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);
    } else if (IsLive()) {
      cpu.TriggerEvent("EnvTrigger", anti_trigger_tags[i]);
    }
  // }

  ++i;

  // is neighbor expired?
  for (size_t lev = 0; lev < cfg.NLEV(); ++lev) {
    if(i >= pro_trigger_tags.size()) {
      pro_trigger_tags.emplace_back(rng);
      auto copy = pro_trigger_tags[i];
      anti_trigger_tags.emplace_back(copy.Toggle());
    }
    if (cfg.CHANNELS_VISIBLE()) {
      if (IsExpired(lev)) {
        cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);
      } else {
        // cpu.TriggerEvent("EnvTrigger", anti_trigger_tags[i]);
      }
    }

    ++i;
  }

  // update trigger
  if(i >= pro_trigger_tags.size()) {
    pro_trigger_tags.emplace_back(rng);
    auto copy = pro_trigger_tags[i];
    anti_trigger_tags.emplace_back(copy.Toggle());
  }
  cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);

  ++i;

  // was just born trigger
  if(i >= pro_trigger_tags.size()) {
    pro_trigger_tags.emplace_back(rng);
    auto copy = pro_trigger_tags[i];
    anti_trigger_tags.emplace_back(copy.Toggle());
  }

  if (Cell().Man().Family(Cell().GetPos()).GetCellAge(update) == 0) {
    cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);
  } else {
  // cpu.TriggerEvent("EnvTrigger", anti_trigger_tags[i]);
  }

  ++i;

  // stochastic trigger
  if(i >= pro_trigger_tags.size()) {
    pro_trigger_tags.emplace_back(rng);
    auto copy = pro_trigger_tags[i];
    anti_trigger_tags.emplace_back(copy.Toggle());
  }

  if (local_rng.GetDouble() < cfg.STOCHASTIC_TRIGGER_FREQ()) {
    cpu.TriggerEvent("EnvTrigger", pro_trigger_tags[i]);
  } else {
  // cpu.TriggerEvent("EnvTrigger", anti_trigger_tags[i]);
  }

}

void FrameHardware::SetupCompute(const size_t update) {
  if (update % cfg.ENV_TRIG_FREQ() == 0) {
    DispatchEnvTriggers(update);
    TryClearStockpileReserve();
    TryClearReproductionReserve();

    emp::vector<Config::matchbin_t::uid_t> marked;
    for (const auto & uid : membrane.ViewUIDs()) {
      auto & v = membrane.GetVal(uid);
      if (v > 2) {
        v -= 2;
      } else {
        membrane_tags.erase(membrane.GetTag(uid));
        marked.push_back(uid);
      }
    }
    for (const auto & uid : marked) {
      membrane.Delete(uid);
    }
  }
}

void FrameHardware::StepProcess() {
  emp_assert(cpu.GetProgram().GetSize());
  cpu.SingleProcess();
}

void FrameHardware::SetProgram(const Config::program_t & program) {
  cpu.SetProgram(program);
}

size_t FrameHardware::GetFacing() const {
  return facing;
}

size_t FrameHardware::GetMsgDir() const {
  return msg_dir;
}

void FrameHardware::SetMsgDir(const size_t new_dir) {
  msg_dir = new_dir;
}

void FrameHardware::SetInboxActivity(const bool state) {
  inbox_active = state;
}

bool FrameHardware::CheckInboxActivity() const {
  return inbox_active;
}

void FrameHardware::QueueMessage(const Config::event_t &event) {
  cpu.QueueEvent(event);
}

void FrameHardware::QueueMessages(Config::inbox_t &inbox) {
  while(inbox_active && !inbox.empty()) {
    if (
      const auto res = membrane.Match(inbox.front().affinity);
      res.size() && (res[0] % 2)
    ) {
      QueueMessage(inbox.front());
    }
    inbox.pop_front();
  }
  // clear inactive inboxes, too!
  inbox.clear();
}

size_t FrameHardware::CalcDir(const double relative_dir) {
  return emp::Mod(
    static_cast<size_t>(relative_dir) + GetFacing(),
    Cardi::NumDirs
  );
}

bool FrameHardware::IsLive(const int relative_dir/*=0*/) {
  const size_t dir = CalcDir(relative_dir);
  const size_t neigh = Cell().GetNeigh(dir);
  return Cell().Man().DW().IsOccupied(neigh);
}

bool FrameHardware::IsOccupied(const int relative_dir/*=0*/) {
  const size_t dir = CalcDir(relative_dir);
  const size_t neigh = Cell().GetNeigh(dir);
  return (bool) Cell().Man().Channel(neigh).GetIDs();
}

bool FrameHardware::IsCellChild(const int relative_dir/*=0*/) {

  const size_t dir = CalcDir(relative_dir);
  const size_t neigh = Cell().GetNeigh(dir);
  Manager &man = Cell().Man();
  const size_t pos = Cell().GetPos();

  return (
    IsLive(relative_dir)
    && man.Family(pos).HasChildPos(neigh)
    && man.Family(neigh).IsParentPos(pos)
  );

}

bool FrameHardware::IsCellParent(const int relative_dir/*=0*/) {

  const size_t dir = CalcDir(relative_dir);
  const size_t neigh = Cell().GetNeigh(dir);
  Manager &man = Cell().Man();
  const size_t pos = Cell().GetPos();

  return (
    IsLive(relative_dir)
    && man.Family(pos).IsParentPos(neigh)
    && man.Family(neigh).HasChildPos(pos)
  );

}

bool FrameHardware::IsChannelMate(const size_t lev, const int relative_dir/*=0*/) {

  const size_t dir = CalcDir(relative_dir);
  const size_t neigh = Cell().GetNeigh(dir);
  Manager &man = Cell().Man();
  const size_t pos = Cell().GetPos();

  // should be able to sense partial apoptosis tiles
  return man.Channel(pos).CheckMatch(man.Channel(neigh), lev);

}

bool FrameHardware::IsPropaguleChild(const int relative_dir/*=0*/) {

  const size_t dir = CalcDir(relative_dir);
  const size_t neigh = Cell().GetNeigh(dir);
  Manager &man = Cell().Man();
  const size_t pos = Cell().GetPos();

  return (
    IsLive(relative_dir)
    && (
      man.Family(neigh).GetPrevChan() == *man.Channel(pos).GetID(cfg.NLEV()-1)
    ));

}

bool FrameHardware::IsPropaguleParent(const int relative_dir/*=0*/) {

  const size_t dir = CalcDir(relative_dir);
  const size_t neigh = Cell().GetNeigh(dir);
  Manager &man = Cell().Man();
  const size_t pos = Cell().GetPos();

  return (
    IsLive(relative_dir)
    && (
      man.Family(pos).GetPrevChan() == *man.Channel(neigh).GetID(cfg.NLEV()-1)
    ));

}

double FrameHardware::IsPoorerThan(const int relative_dir/*=0*/) {

  const size_t dir = CalcDir(relative_dir);
  const size_t neigh = Cell().GetNeigh(dir);
  Manager &man = Cell().Man();
  const size_t pos = Cell().GetPos();

  return std::max(
    man.Stockpile(neigh).QueryResource() - man.Stockpile(pos).QueryResource(),
    0.0
  );

}

bool FrameHardware::IsOlderThan(const int relative_dir/*=0*/) {

  const size_t dir = CalcDir(relative_dir);
  const size_t neigh = Cell().GetNeigh(dir);
  Manager &man = Cell().Man();
  const size_t pos = Cell().GetPos();

  return (
    man.Family(pos).GetBirthUpdate() <= man.Family(neigh).GetBirthUpdate()
  );

}

size_t FrameHardware::IsExpired(
  const size_t lev,
  const int relative_dir/*=0*/
) {

  const size_t dir = CalcDir(relative_dir);
  const size_t neigh = Cell().GetNeigh(dir);
  Manager &man = Cell().Man();

  return man.Channel(neigh).IsExpired(lev);

}

const Config::hardware_t& FrameHardware::GetHardware() {
  return cpu;
}

void FrameHardware::SetRegulators(Config::matchbin_t & target_mb) {

  cpu.GetMatchBin().ImprintRegulators(target_mb);

}

Config::matchbin_t & FrameHardware::GetMembrane() { return membrane; }

std::unordered_map<
  Config::matchbin_t::tag_t,
  Config::matchbin_t::uid_t
> & FrameHardware::GetMembraneTags() { return membrane_tags; }
