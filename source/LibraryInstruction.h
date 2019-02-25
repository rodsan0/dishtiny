#pragma once

#include <functional>
#include <cmath>

#include "base/assert.h"
#include "tools/Random.h"
#include "tools/string_utils.h"
#include "hardware/InstLib.h"

#include "FrameHardware.h"
#include "Config.h"
#include "GeometryHelper.h"

class LibraryInstruction {

private:

  using hardware_t = Config::hardware_t;
  using inst_lib_t = Config::inst_lib_t;
  using inst_t = inst_lib_t::inst_t;
  using state_t = hardware_t::State;

  static void TRL(hardware_t & hw, const double arg, const size_t lev, const Config &cfg){

    FrameHardware &fh = *hw.GetTrait(0);

    Manager &man = fh.Cell().Man();
    const size_t pos = fh.Cell().GetPos();

    if( fh.IsReprPaused(lev)
      || cfg.REP_THRESH() + fh.CheckStockpileReserve() > man.Stockpile(pos).QueryResource()
    ) {
      return;
    } else if (man.Channel(pos).IsExpired(lev)) {
      TRL(hw, arg, lev+1, cfg);
    } else {
      man.Stockpile(pos).RequestResourceAmt(cfg.REP_THRESH());

      const size_t dir = fh.CalcDir(arg);
      man.Priority(fh.Cell().GetNeigh(dir)).ProcessSire({
          pos,
          dir,
          lev,
          man.Channel(pos).GetGenCounter(),
          *man.Channel(pos).GetIDs(),
          lev < cfg.NLEV() ? man.Family(pos).GetPrevChan() : *man.Channel(pos).GetID(cfg.NLEV() - 1)
        },
        hw.GetProgram()
      );
    }

  }


public:

  static void InitDefault(inst_lib_t &il) {

    il.AddInst("Inc", Config::hardware_t::Inst_Inc, 1, "Increment value in local memory Arg1");
    il.AddInst("Dec", Config::hardware_t::Inst_Dec, 1, "Decrement value in local memory Arg1");
    il.AddInst("Not", Config::hardware_t::Inst_Not, 1, "Logically toggle value in local memory Arg1");
    il.AddInst("Add", Config::hardware_t::Inst_Add, 3, "Local memory: Arg3 = Arg1 + Arg2");
    il.AddInst("Sub", Config::hardware_t::Inst_Sub, 3, "Local memory: Arg3 = Arg1 - Arg2");
    il.AddInst("Mult", Config::hardware_t::Inst_Mult, 3, "Local memory: Arg3 = Arg1 * Arg2");
    il.AddInst("Div", Config::hardware_t::Inst_Div, 3, "Local memory: Arg3 = Arg1 / Arg2");
    il.AddInst("Mod", Config::hardware_t::Inst_Mod, 3, "Local memory: Arg3 = Arg1 % Arg2");
    il.AddInst("TestEqu", Config::hardware_t::Inst_TestEqu, 3, "Local memory: Arg3 = (Arg1 == Arg2)");
    il.AddInst("TestNEqu", Config::hardware_t::Inst_TestNEqu, 3, "Local memory: Arg3 = (Arg1 != Arg2)");
    il.AddInst("TestLess", Config::hardware_t::Inst_TestLess, 3, "Local memory: Arg3 = (Arg1 < Arg2)");
    il.AddInst("If", Config::hardware_t::Inst_If, 1, "Local memory: If Arg1 != 0, proceed; else, skip block.", emp::ScopeType::BASIC, 0, {"block_def"});
    il.AddInst("While", Config::hardware_t::Inst_While, 1, "Local memory: If Arg1 != 0, loop; else, skip block.", emp::ScopeType::BASIC, 0, {"block_def"});
    il.AddInst("Countdown", Config::hardware_t::Inst_Countdown, 1, "Local memory: Countdown Arg1 to zero.", emp::ScopeType::BASIC, 0, {"block_def"});
    il.AddInst("Close", Config::hardware_t::Inst_Close, 0, "Close current block if there is a block to close.", emp::ScopeType::BASIC, 0, {"block_close"});
    il.AddInst("Break", Config::hardware_t::Inst_Break, 0, "Break out of current block.");
    il.AddInst("Call", Config::hardware_t::Inst_Call, 0, "Call function that best matches call affinity.", emp::ScopeType::BASIC, 0, {"affinity"});
    il.AddInst("Return", Config::hardware_t::Inst_Return, 0, "Return from current function if possible.");
    il.AddInst("SetMem", Config::hardware_t::Inst_SetMem, 2, "Local memory: Arg1 = numerical value of Arg2");
    il.AddInst("CopyMem", Config::hardware_t::Inst_CopyMem, 2, "Local memory: Arg1 = Arg2");
    il.AddInst("SwapMem", Config::hardware_t::Inst_SwapMem, 2, "Local memory: Swap values of Arg1 and Arg2.");
    il.AddInst("Input", Config::hardware_t::Inst_Input, 2, "Input memory Arg1 => Local memory Arg2.");
    il.AddInst("Output", Config::hardware_t::Inst_Output, 2, "Local memory Arg1 => Output memory Arg2.");
    il.AddInst("Commit", Config::hardware_t::Inst_Commit, 2, "Local memory Arg1 => Shared memory Arg2.");
    il.AddInst("Pull", Config::hardware_t::Inst_Pull, 2, "Shared memory Arg1 => Shared memory Arg2.");
    il.AddInst("Fork", Config::hardware_t::Inst_Fork, 0, "Fork a new thread, using tag-based referencing to determine which function to call on the new thread.", emp::ScopeType::BASIC, 0, {"affinity"});
    il.AddInst("Terminate", Config::hardware_t::Inst_Terminate, 0, "Terminate current thread.");
    il.AddInst("Nop", Config::hardware_t::Inst_Nop, 0, "No operation.");
    il.AddInst("Rng",
      [](hardware_t &hw, const inst_t &inst){
        state_t & state = hw.GetCurState();
        state.SetLocal(inst.args[0], hw.GetRandom().GetDouble());
      },
      1,
      "Draw from onboard random number generator."
    );
  }

  static void InitInternalActions(inst_lib_t &il, const Config &cfg) {

    il.AddInst(
      "SendMsgInternal",
      [](hardware_t & hw, const inst_t & inst){
        FrameHardware &fh = *hw.GetTrait(0);
        const state_t & state = hw.GetCurState();
        const size_t dir = fh.CalcDir(state.GetLocal(inst.args[0]));
        fh.SetMsgDir(dir);
        hw.TriggerEvent("SendMsgInternal", inst.affinity, state.output_mem);
      },
      1,
      "Send a single message to a target.",
      emp::ScopeType::BASIC,
      0,
      {"affinity"}
    );

    il.AddInst(
      "BcstMsgInternal",
      [](hardware_t & hw, const inst_t & inst){
        FrameHardware &fh = *hw.GetTrait(0);
        const state_t & state = hw.GetCurState();

        for(size_t dir = 0; dir < Cardi::NumDirs; ++dir) {
          if (dir == fh.GetFacing()) continue;
          fh.SetMsgDir(dir);
          hw.TriggerEvent("SendMsgInternal", inst.affinity, state.output_mem);
        }
      },
      0,
      "Send a single message to a target.",
      emp::ScopeType::BASIC,
      0,
      {"affinity"}
    );

    il.AddInst(
      "IncrStockpileReserve",
      [&cfg](hardware_t & hw, const inst_t & inst){
        FrameHardware &fh = *hw.GetTrait(0);

        const state_t & state = hw.GetCurState();
        const size_t dir = fh.CalcDir(state.GetLocal(inst.args[0]));

        fh.Cell().GetFrameHardware(dir).AdjustStockpileReserve(
          cfg.REP_THRESH()/2
        );
      },
      1,
      "TODO"
    );

    il.AddInst(
      "DecrStockpileReserve",
      [&cfg](hardware_t & hw, const inst_t & inst){
        FrameHardware &fh = *hw.GetTrait(0);

        const state_t & state = hw.GetCurState();
        const size_t dir = fh.CalcDir(state.GetLocal(inst.args[0]));

        fh.Cell().GetFrameHardware(dir).AdjustStockpileReserve(
          -cfg.REP_THRESH()/2
        );
      },
      1,
      "TODO"
    );

    for(size_t replev = 0; replev < cfg.NLEV()+1; ++replev) {
      il.AddInst(
        "PauseRepr-Lev" + emp::to_string(replev),
        [replev](hardware_t & hw, const inst_t & inst){

          FrameHardware &fh = *hw.GetTrait(0);

          const state_t & state = hw.GetCurState();
          const size_t dir = fh.CalcDir(state.GetLocal(inst.args[0]));

          fh.Cell().GetFrameHardware(dir).PauseRepr(replev);

        },
        1,
        "TODO"
      );
    }

    il.AddInst(
      "PauseRepr",
      [&cfg](hardware_t & hw, const inst_t & inst){

        FrameHardware &fh = *hw.GetTrait(0);

        const state_t & state = hw.GetCurState();
        const size_t dir = fh.CalcDir(state.GetLocal(inst.args[0]));

        for(size_t i = 0; i < cfg.NLEV()+1; ++i) {
          fh.Cell().GetFrameHardware(dir).PauseRepr(i);
        }

      },
      1,
      "TODO"
    );


    il.AddInst(
      "ActivateInbox",
      [](hardware_t &hw, const inst_t &inst){
        FrameHardware &fh = *hw.GetTrait(0);
        const state_t & state = hw.GetCurState();
        const size_t dir = fh.CalcDir(state.GetLocal(inst.args[0]));
        fh.Cell().GetFrameHardware(dir).SetInboxActivity(true);
      },
      1,
      "TODO"
    );

    il.AddInst(
      "DeactivateInbox",
      [](hardware_t &hw, const inst_t &inst){
        FrameHardware &fh = *hw.GetTrait(0);
        const state_t & state = hw.GetCurState();
        const size_t dir = fh.CalcDir(state.GetLocal(inst.args[0]));
        fh.Cell().GetFrameHardware(dir).SetInboxActivity(false);
      },
      1,
      "TODO"
    );

  }

  static void InitExternalActions(inst_lib_t &il, const Config &cfg) {

    il.AddInst(
      "SendBigFracResource",
      [](hardware_t & hw, const inst_t & inst){

        FrameHardware &fh = *hw.GetTrait(0);
        const state_t & state = hw.GetCurState();

        const size_t dir = fh.CalcDir(state.GetLocal(inst.args[0]));
        if(!fh.IsLive(dir)) return;

        Manager &man = fh.Cell().Man();
        const size_t pos = fh.Cell().GetPos();

        const double amt = man.Stockpile(pos).RequestResourceFrac(0.5);

        const size_t neigh = fh.Cell().GetNeigh(dir);

        man.Stockpile(neigh).ExternalContribute(amt,Cardi::Opp[dir]);
      },
      1,
      "TODO"
    );

    il.AddInst(
      "SendSmallFracResource",
      [](hardware_t & hw, const inst_t & inst){

        FrameHardware &fh = *hw.GetTrait(0);
        const state_t & state = hw.GetCurState();

        const size_t dir = fh.CalcDir(state.GetLocal(inst.args[0]));
        if(!fh.IsLive(dir)) return;

        Manager &man = fh.Cell().Man();
        const size_t pos = fh.Cell().GetPos();

        const double amt = man.Stockpile(pos).RequestResourceFrac(0.02);

        const size_t neigh = fh.Cell().GetNeigh(dir);

        man.Stockpile(neigh).ExternalContribute(amt,Cardi::Opp[dir]);
      },
      1,
      "TODO"
    );

    il.AddInst(
      "SendMsgExternal",
      [](hardware_t & hw, const inst_t & inst){
        FrameHardware &fh = *hw.GetTrait(0);
        const state_t & state = hw.GetCurState();
        const size_t dir = fh.CalcDir(state.GetLocal(inst.args[0]));
        fh.SetMsgDir(dir);
        hw.TriggerEvent("SendMsgExternal", inst.affinity, state.output_mem);
      },
      1,
      "Send a single message to a target.",
      emp::ScopeType::BASIC,
      0,
      {"affinity"}
    );

    il.AddInst(
      "BcstMsgExternal",
      [](hardware_t & hw, const inst_t & inst){
        FrameHardware &fh = *hw.GetTrait(0);
        const state_t & state = hw.GetCurState();

        for(size_t dir = 0; dir < Cardi::NumDirs; ++dir) {
          fh.SetMsgDir(dir);
          hw.TriggerEvent("SendMsgExternal", inst.affinity, state.output_mem);
        }

      },
      0,
      "Send a message to all neighbors.",
      emp::ScopeType::BASIC,
      0,
      {"affinity"}
    );

    for(size_t replev = 0; replev < cfg.NLEV()+1; ++replev) {
      il.AddInst(
        "TryReproduce-Lev" + emp::to_string(replev),
        [replev, &cfg](hardware_t & hw, const inst_t & inst){
          const state_t & state = hw.GetCurState();
          TRL(hw, state.GetLocal(inst.args[0]), replev, cfg);
        },
        1,
        "TODO"
      );
    }

    for(size_t lev = 0; lev < cfg.NLEV(); ++lev) {
      il.AddInst(
        "IncrCellAge-Lev" + emp::to_string(lev),
        [lev, &cfg](hardware_t & hw, const inst_t & inst){
          const state_t & state = hw.GetCurState();
          FrameHardware &fh = *hw.GetTrait(0);
          fh.Cell().Man().Channel(fh.Cell().GetPos()).IncrCellAge(
            lev,
            std::max(0.0,std::min(1+state.GetLocal(inst.args[0]), 1000000.0))
          );
        },
        1,
        "TODO"
      );
    }

    il.AddInst(
      "DoApoptosisComplete",
      [](hardware_t & hw, const inst_t & inst){

        FrameHardware &fh = *hw.GetTrait(0);

        Manager &man = fh.Cell().Man();
        const size_t pos = fh.Cell().GetPos();

        man.Apoptosis(pos).MarkComplete();

      },
      0,
      "TODO"
    );

    il.AddInst(
      "DoApoptosisPartial",
      [](hardware_t & hw, const inst_t & inst){

        FrameHardware &fh = *hw.GetTrait(0);

        Manager &man = fh.Cell().Man();
        const size_t pos = fh.Cell().GetPos();

        man.Apoptosis(pos).MarkPartial();

      },
      0,
      "TODO"
    );

  }

  static void InitInternalSensors(inst_lib_t &il, const Config &cfg) {
    il.AddInst(
      "QueryOwnStockpile",
      [](hardware_t & hw, const inst_t & inst){

        FrameHardware &fh = *hw.GetTrait(0);

        Manager &man = fh.Cell().Man();
        const size_t pos = fh.Cell().GetPos();
        const double amt = man.Stockpile(pos).QueryResource();

        state_t & state = hw.GetCurState();

        state.SetLocal(inst.args[0], amt);

      },
      1,
      "TODO"
    );

    for(size_t l = 0; l < cfg.NLEV(); ++l) {
      il.AddInst(
        "QueryChannelGen-Lev"+emp::to_string(l),
        [l](hardware_t & hw, const inst_t & inst){

          FrameHardware &fh = *hw.GetTrait(0);

          Manager &man = fh.Cell().Man();
          const size_t pos = fh.Cell().GetPos();
          const size_t gen = man.Channel(pos).GetGeneration(l);

          state_t & state = hw.GetCurState();

          state.SetLocal(inst.args[0], gen);

        },
        1,
        "TODO"
      );
    }

  }

  static void InitExternalSensors(inst_lib_t &il, const Config &cfg) {

    il.AddInst(
      "QueryIsLive",
      [](hardware_t & hw, const inst_t & inst){

        state_t & state = hw.GetCurState();
        FrameHardware &fh = *hw.GetTrait(0);

        state.SetLocal(
          inst.args[1],
          fh.IsLive(state.GetLocal(inst.args[0]))
        );

      },
      2,
      "TODO"
    );

    il.AddInst(
      "QueryIsOccupied",
      [](hardware_t & hw, const inst_t & inst){

        state_t & state = hw.GetCurState();
        FrameHardware &fh = *hw.GetTrait(0);

        state.SetLocal(
          inst.args[1],
          fh.IsOccupied(state.GetLocal(inst.args[0]))
        );

      },
      2,
      "TODO"
    );

    il.AddInst(
      "QueryIsCellChild",
      [](hardware_t & hw, const inst_t & inst){

        state_t & state = hw.GetCurState();
        FrameHardware &fh = *hw.GetTrait(0);

        state.SetLocal(
          inst.args[1],
          fh.IsCellChild(state.GetLocal(inst.args[0]))
        );

      },
      2,
      "TODO"
    );

    il.AddInst(
      "QueryIsCellParent",
      [](hardware_t & hw, const inst_t & inst){

        state_t & state = hw.GetCurState();
        FrameHardware &fh = *hw.GetTrait(0);

        state.SetLocal(
          inst.args[1],
          fh.IsCellParent(state.GetLocal(inst.args[0]))
        );

      },
      2,
      "TODO"
    );

    for (size_t lev = 0; lev < cfg.NLEV(); ++lev) {
      il.AddInst(
        "QueryFacingChannelMate-Lev" + emp::to_string(lev),
        [lev](hardware_t & hw, const inst_t & inst){

          state_t & state = hw.GetCurState();
          FrameHardware &fh = *hw.GetTrait(0);

          state.SetLocal(
            inst.args[1],
            fh.IsChannelMate(lev, state.GetLocal(inst.args[0]))
          );

        },
        2,
        "TODO"
      );
    }

    il.AddInst(
      "QueryIsPropaguleChild",
      [&cfg](hardware_t & hw, const inst_t & inst){

        state_t & state = hw.GetCurState();
        FrameHardware &fh = *hw.GetTrait(0);

        state.SetLocal(
          inst.args[1],
          fh.IsPropaguleChild(state.GetLocal(inst.args[0]))
        );

      },
      2,
      "TODO"
    );

    il.AddInst(
      "QueryIsPropaguleParent",
      [&cfg](hardware_t & hw, const inst_t & inst){
        state_t & state = hw.GetCurState();

        FrameHardware &fh = *hw.GetTrait(0);

        state.SetLocal(
          inst.args[1],
          fh.IsPropaguleParent(state.GetLocal(inst.args[0]))
        );

      },
      2,
      "TODO"
    );

    // get the raw channel of who is next door
    // potentially useful for aggregate count of distinct neighbors
    for (size_t i = 0; i < cfg.NLEV(); ++i) {
      il.AddInst(
        "QueryFacingChannel-Lev" + emp::to_string(i),
        [i](hardware_t & hw, const inst_t & inst){
          state_t & state = hw.GetCurState();

          FrameHardware &fh = *hw.GetTrait(0);

          const size_t dir = fh.CalcDir(state.GetLocal(inst.args[0]));
          const size_t neigh = fh.Cell().GetNeigh(dir);

          if(fh.IsLive(state.GetLocal(inst.args[0]))) {
            Manager &man = fh.Cell().Man();
            auto chanid = man.Channel(neigh).GetID(i);
            if(chanid) state.SetLocal(inst.args[1], *chanid);
          } else {
            state.SetLocal(inst.args[1], false);
          }
        },
        2,
        "TODO"
      );
    }

    il.AddInst(
      "QueryFacingStockpile",
      [](hardware_t & hw, const inst_t & inst){
        state_t & state = hw.GetCurState();

        FrameHardware &fh = *hw.GetTrait(0);

        const size_t dir = fh.CalcDir(state.GetLocal(inst.args[0]));
        const size_t neigh = fh.Cell().GetNeigh(dir);

        if(fh.IsLive(state.GetLocal(inst.args[0]))) {
          Manager &man = fh.Cell().Man();
          const double amt = man.Stockpile(neigh).QueryResource();
          state.SetLocal(inst.args[1], amt);
        } else {
          state.SetLocal(inst.args[1], false);
        }
      },
      2,
      "TODO"
    );

  }

  static const inst_lib_t& Make(const Config &cfg) {

    static inst_lib_t il;

    if (il.GetSize() == 0) {

      InitDefault(il);

      InitInternalActions(il, cfg);

      InitInternalSensors(il, cfg);

      InitExternalActions(il, cfg);

      InitExternalSensors(il, cfg);

      std::cout << "Instruction Library Size: " << il.GetSize() << std::endl;

    }

    emp_assert(il.GetSize());

    return il;

  }

};