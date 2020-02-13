#include <cstdio>
#include <list>
#include <forward_list>
#include <string>
#include <unordered_map>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>

#include <minecraft/json.h>
#include <cmdhelper.h>
#include <Loader.h>
//#include <MC.h>
#include <minecraft/core/getSP.h>
#include <minecraft/actor/Player.h>

#include "base.h"
#include "../gui/gui.h"
#include "PlayerMap.h"
#include "tp.command.h"

const char meta[] __attribute__((used, section("meta"))) =
    "name:tp\n"
    "version:20200204\n"
    "author:sysca11\n"
    "depend:base@20200121,command@20200121,gui@20200121\n";

using std::forward_list;
using std::string;
using std::unordered_map;
//#define dbg_printf(...) {}
#define dbg_printf printf
extern "C" {
BDL_EXPORT void mod_init(std::list<string> &modlist);
}

extern void load_helper(std::list<string> &modlist);
static bool CanBack = true, CanHome = true, CanTP = true;
int MaxHomes = 5;
struct Vpos {
  int x, y, z, dim;
  string name;
  Vpos() {}
  Vpos(int a, int b, int c, int d, string_view e) { x = a, y = b, z = c, dim = d, name = e; }
  void packto(DataStream &ds) const { ds << x << y << z << dim << name; }
  void unpack(DataStream &ds) { ds >> x >> y >> z >> dim >> name; }
  void tele(Actor &ply) const { TeleportA(ply, {(float) x, (float) y, (float) z}, {dim}); }
};
static list<string> warp_list;
static unordered_map<string, Vpos> warp;
static LDBImpl tp_db("data_v2/tp");
struct home {
  int cnt = 0;
  vector<Vpos> vals;
  void packto(DataStream &ds) const { ds << vals; }
  void unpack(DataStream &ds) {
    ds >> vals;
    cnt = vals.size();
  }
  home(ServerPlayer &sp) {
    DataStream ds;
    if (tp_db.Get("home_" + sp.getName(), ds.dat)) { ds >> *this; }
  }
  void save(ServerPlayer &sp) {
    DataStream ds;
    ds << *this;
    tp_db.Put("home_" + sp.getName(), ds.dat);
  }
};

void add_warp(int x, int y, int z, int dim, const string &name) {
  warp_list.push_back(name);
  Vpos ps;
  ps.x = x, ps.y = y, ps.z = z, ps.dim = dim, ps.name = name;
  warp[name] = ps;
  DataStream ds;
  ds << ps;
  tp_db.Put("warp_" + name, ds.dat);
  ds.reset();
  ds << warp_list;
  tp_db.Put("warps", ds.dat);
}
void del_warp(const string &name) {
  warp_list.remove(name);
  DataStream ds;
  ds << warp_list;
  tp_db.Put("warps", ds.dat);
  tp_db.Del("warp_" + name);
}
void load_warps_new() {
  DataStream ds;
  if (!tp_db.Get("warps", ds.dat)) return; // fix crash
  ds >> warp_list;
  do_log("%ld warps found", warp_list.size());
  for (auto &i : warp_list) {
    DataStream tmpds;
    tp_db.Get("warp_" + i, tmpds.dat);
    tmpds >> warp[i];
  }
}
/*
static unordered_map<string, home> home_cache;
static home &getHome(const string &key) {
  auto it = home_cache.find(key);
  if (it != home_cache.end()) { return it->second; }
  if (home_cache.size() > 256) { home_cache.clear(); }
  DataStream ds;
  home hm;
  if (tp_db.Get("home_" + key, ds.dat)) { ds >> hm; }
  home_cache[key] = hm;
  return home_cache[key];
}
static void putHome(const string &key, home &hm) {
  DataStream ds;
  ds << hm;
  tp_db.Put("home_" + key, ds.dat);
}*/
PlayerMap<home> ply_homes;
struct tpreq {
  int dir; // 0=f
  string name;
  clock_t reqtime;
};
static unordered_map<string, tpreq> tpmap;
static void oncmd_suic(argVec &a, CommandOrigin const &b, CommandOutput &outp) {
  auto sp = getSP(b.getEntity());
  if (sp) {
    ((Mob *) sp)->kill();
    outp.success("诶唷！那看上去很疼");
  }
}

static void sendTPChoose(ServerPlayer *sp, int type) { // 0=t
//  string name = sp->getNameTag();
  gui_ChoosePlayer(sp, "选择目标玩家", "发送传送请求", [type](ServerPlayer *xx, string_view dest) {
    SPBuf<512> sb;
    sb.write("tpa "sv);
    if (type == 0)
      sb.write("t");
    else
      sb.write("f");
    sb.write(" \""sv);
    sb.write(dest);
    sb.write("\""sv);
    runcmdAs(sb.get(), xx);
  });
}

static void sendTPForm(const string &from, int type, ServerPlayer *sp) {
  SPBuf<512> sb;
  sb.write("§b ");
  sb.write(from);
  sb.write((type ? " 想传送到你的位置" : " 想将您传送到他的位置"));
  sb.write(",您可以键入 \"/tpa ac\" 来同意或键入 \"/tpa de\" 来拒绝");
  sendText(sp, sb);
  sb.clear();
  sb.write(from);
  sb.write((type ? " 想传送到你的位置" : " 想将您传送到他的位置"));
  SharedForm *sf = getForm("TP Request", sb.get());
  sf->addButton("同意传送请求");
  sf->addButton("拒绝传送请求");
  sf->cb = [](ServerPlayer *sp, string_view choice, int idx) {
    idx == 0 ? runcmdAs("tpa ac", sp) : runcmdAs("tpa de", sp);
  };
  sendForm(*sp, sf);
}
SharedForm TPGUI("Send teleport request", "Send teleport request", false);
static void SendTPGUI(ServerPlayer *sp) { sendForm(*sp, &TPGUI); }
static void initTPGUI() {
  TPGUI.addButton("传送到玩家");
  TPGUI.addButton("传送玩家到你");
  TPGUI.cb = [](ServerPlayer *sp, string_view sv, int idx) { sendTPChoose(sp, idx); };
}
static unordered_map<string, string> player_target;
void TPACommand::invoke(mandatory<TPCMD> mode, optional<string> target) {
  if (!CanTP) {
    getOutput().error("传送功能被服务器禁用。");
    return;
  }
  auto sp = getSP(getOrigin().getEntity());
  if (!sp) return;
  auto &nam = sp->getNameTag();
  switch (mode) {
  case TPCMD::ac: {
    if (tpmap.count(nam) == 0) return;
    tpreq &req = tpmap[nam];
    getOutput().success("§b你接受了对方的传送请求");
    player_target.erase(req.name);
    auto dst = getplayer_byname(req.name);
    if (dst) {
      SPBuf sb;
      sb.write("§b ");
      sb.write(nam);
      sb.write(" 接受了传送请求");
      sendText(dst, sb);
      if (req.dir == 0) {
        // f
        TeleportA(*sp, dst->getPos(), {dst->getDimensionId()});
      } else {
        TeleportA(*dst, sp->getPos(), {sp->getDimensionId()});
      }
    }
    tpmap.erase(nam);
  } break;
  case TPCMD::de: {
    if (tpmap.count(nam) == 0) return;
    tpreq &req = tpmap[nam];
    getOutput().success("§b你拒绝了传送请求");
    player_target.erase(req.name);
    auto dst = getplayer_byname(req.name);
    if (dst) {
      SPBuf sb;
      sb.write("§b ");
      sb.write(nam);
      sb.write(" 拒绝了传送请求");
      sendText(dst, sb);
    }
    tpmap.erase(nam);
  } break;
  case TPCMD::cancel: {
    if (player_target.count(nam)) {
      auto &nm = player_target[nam];
      if (tpmap.count(nm) && tpmap[nm].name == nam) {
        tpmap.erase(nm);
        getOutput().success("撤销传送请求成功");
      }
    }
  } break;
  case TPCMD::gui: {
    SendTPGUI(sp);
    getOutput().success();
  } break;
  case TPCMD::f: {
    auto dst = getplayer_byname2(target);
    if (!dst) {
      getOutput().error("目标未找到！");
      return;
    }
    auto &dnm = dst->getNameTag();
    if (tpmap.count(dnm)) {
      getOutput().error("您的传送请求正在处理中");
      return;
    }
    if (player_target.count(dnm)) {
      getOutput().error("您已经发起了一个传送请求");
      return;
    }
    player_target[nam] = dnm;
    tpmap[dnm]         = {0, nam, clock()};
    getOutput().success("§b已将传送请求发送给目标玩家");
    sendTPForm(nam, 0, (ServerPlayer *) dst);
    return;
  }
  case TPCMD::t: {
    auto dst = getplayer_byname2(target);
    if (!dst) {
      getOutput().error("目标未找到！");
      return;
    }
    auto &dnm = dst->getNameTag();
    if (tpmap.count(dnm)) {
      getOutput().error("您的传送请求正在处理中");
      return;
    }
    if (player_target.count(dnm)) {
      getOutput().error("您已经发起了一个传送请求");
      return;
    }
    player_target[nam] = dnm;
    tpmap[dnm]         = {1, nam, clock()};
    getOutput().success("§b已将传送请求发送给目标玩家");
    sendTPForm(nam, 1, (ServerPlayer *) dst);
    return;
  }
  default: break;
  }
}
static void oncmd_home(argVec &a, CommandOrigin const &b, CommandOutput &outp) {
  if (!CanHome) {
    outp.error("Home 功能未启用。请联系你的服务器管理员。");
    return;
  }
  ServerPlayer *sp = getSP(b.getEntity());
  if (!sp) {
    outp.error("这个命令仅供玩家使用");
    return;
  }
  Vec3 pos          = b.getWorldPosition();
  ARGSZ(1)
  if (a[0] == "add") {
    ARGSZ(2)
    home &myh = ply_homes[sp];
    if (myh.cnt >= MaxHomes) {
      outp.error("你不能再添加你的家了！你的家太多了");
      return;
    }
    myh.vals.push_back(Vpos(pos.x, pos.y, pos.z, b.getEntity()->getDimensionId(), a[1]));
    myh.cnt++;
    myh.save(*sp);
    outp.success("§b成功添加家");
  }
  if (a[0] == "del") {
    ARGSZ(2)
    home &myh = ply_homes[sp];
    for (auto i = myh.vals.begin(); i != myh.vals.end(); ++i) {
      if (i->name == a[1]) {
        myh.vals.erase(i);
        outp.success("§b此家已被删除");
        myh.save(*sp);
        return;
      }
    }
    outp.error("删除请求无法完成，未能找到家。");
  }
  if (a[0] == "go") {
    ARGSZ(2)
    home &myh = ply_homes[sp];
    for (int i = 0; i < myh.cnt; ++i) {
      if (myh.vals[i].name == a[1]) {
        myh.vals[i].tele(*sp);
        outp.success("§b已传送到家);
      }
    }
  }
  if (a[0] == "ls") {
    home &myh = ply_homes[sp];
    outp.addMessage("§b====Home 列表====");
    for (int i = 0; i < myh.cnt; ++i) outp.addMessage(myh.vals[i].name);
    outp.success("§b============");
  }
  if (a[0] == "gui") {
    home &myh = ply_homes[sp];
    auto sf   = getForm("家", "请选择一个家");
    for (int i = 0; i < myh.cnt; ++i) {
      auto &hname = myh.vals[i].name;
      sf->addButton(hname);
    }
    sf->cb = [](ServerPlayer *sp, string_view sv, int idx) {
      SPBuf<512> sb;
      sb.write("home go \"");
      sb.write(sv);
      sb.write("\"");
      runcmdAs(sb.get(), sp);
    };
    sendForm(*sp, sf);
    outp.success();
  }
  if (a[0] == "delgui") {
    home &myh = ply_homes[sp];
    auto sf   = getForm("家", "请选择一个家来删除");
    for (int i = 0; i < myh.cnt; ++i) {
      auto &hname = myh.vals[i].name;
      sf->addButton(hname);
    }
    sf->cb = [](ServerPlayer *sp, string_view sv, int idx) {
      SPBuf<512> sb;
      sb.write("home del \"");
      sb.write(sv);
      sb.write("\"");
      runcmdAs(sb.get(), sp);
    };
    sendForm(*sp, sf);
    outp.success();
  }
}
static void oncmd_warp(argVec &a, CommandOrigin const &b, CommandOutput &outp) {
  int pl = (int) b.getPermissionsLevel();
  // do_log("pl %d",pl);
  string name = b.getName();
  Vec3 pos    = b.getWorldPosition();
  ARGSZ(1)
  if (a[0] == "add") {
    if (pl < 1) return;
    ARGSZ(2)
    add_warp(pos.x, pos.y, pos.z, b.getEntity()->getDimensionId(), string(a[1]));
    outp.success("§b成功增加地标");
    return;
  }
  if (a[0] == "del") {
    if (pl < 1) return;
    ARGSZ(2)
    del_warp(string(a[1]));
    outp.success("§b成功删除地标");
    return;
  }
  if (a[0] == "ls") {
    outp.addMessage("§b====地标列表====");
    for (auto const &i : warp_list) { outp.addMessage(i); }
    outp.success("§b===========");
    return;
  }
  if (a[0] == "gui") {
    auto sf = getForm("家", "请选择一个家");
    for (auto const &i : warp_list) {
      auto &hname = i;
      sf->addButton(hname);
    }
    sf->cb = [](ServerPlayer *sp, string_view sv, int idx) {
      SPBuf<512> sb;
      sb.write("warp \"");
      sb.write(sv);
      sb.write("\"");
      runcmdAs(sb.get(), sp);
    };
    sendForm(*(ServerPlayer *) b.getEntity(), sf);
    outp.success();
  }
  // go
  auto it = warp.find(string(a[0]));
  if (it != warp.end()) {
    it->second.tele(*b.getEntity());
    outp.success("§b成功传送到地标");
    return;
  }
}

static unordered_map<string, pair<Vec3, int>> deathpoint;
static void oncmd_back(argVec &a, CommandOrigin const &b, CommandOutput &outp) {
  if (!CanBack) {
    outp.error("/Back 功能未启用。请联系你的服务器管理员。");
    return;
  }
  ServerPlayer *sp = (ServerPlayer *) b.getEntity();
  if (!sp) return;
  auto it = deathpoint.find(sp->getNameTag());
  if (it == deathpoint.end()) {
    outp.error("未能找到死亡点，或该死亡点已被传送过。");
    return;
  }
  TeleportA(*sp, it->second.first, {it->second.second});
  deathpoint.erase(it);
  outp.success("§b成功返回死亡点");
}
static void handle_mobdie(Mob &mb, const ActorDamageSource &) {
  if (!CanBack) return;
  auto sp = getSP(mb);
  if (sp) {
    ServerPlayer *sp = (ServerPlayer *) &mb;
    sendText(sp, "§b你可以键入 /back 来返回上一死亡点");
    deathpoint[sp->getNameTag()] = {sp->getPos(), sp->getDimensionId()};
  }
}
static int TP_TIMEOUT = 30;
THook(void *, _ZN12ServerPlayer9tickWorldERK4Tick, ServerPlayer *sp, unsigned long const *tk) {
  auto res = original(sp, tk);
  if (*tk % 40 == 0) {
    auto &name = sp->getNameTag();
    auto it    = tpmap.find(name);
    if (it != tpmap.end()) {
      if (it->second.reqtime + CLOCKS_PER_SEC * TP_TIMEOUT <= clock()) {
        player_target.erase(it->second.name);
        sendText(sp, "您拒绝了TP请求（超时）");
        auto sp2 = getuser_byname(it->second.name);
        sendText(sp2, "目标拒绝了TP请求（超时）");
        tpmap.erase(it);
      }
    }
  }
  return res;
}

static void load_cfg() {
  std::ifstream ifs{"config/tp.json"};
  Json::Value value;
  Json::Reader reader;
  if (!reader.parse(ifs, value)) {
    auto msg = reader.getFormattedErrorMessages();
    do_log("%s", msg.c_str());
    exit(1);
  }
  CanBack       = value["can_back"].asBool(false);
  CanHome       = value["can_home"].asBool(false);
  CanTP         = value["can_tp"].asBool(false);
  MaxHomes      = value["max_homes"].asInt(5);
  auto &timeout = value["TP_TIMEOUT"];
  if (timeout.isInt()) {
    TP_TIMEOUT = timeout.asInt(30);
  } else {
    do_log("NO TP_TIMEOUT FOUND!USE 30s AS DEFAULT!!!");
  }
}

void mod_init(std::list<string> &modlist) {
  do_log("loaded! " BDL_TAG "");
  load_warps_new();
  load_cfg();
  initTPGUI();
  register_cmd("suicide", oncmd_suic, "自杀");
  register_cmd("home", oncmd_home, "创建，删除或传送到家");
  register_cmd("warp", oncmd_warp, "创建，删除或传送到地标");
  register_cmd("back", oncmd_back, "返回上一死亡点");
  reg_mobdie(handle_mobdie);
  register_commands();
  load_helper(modlist);
}
