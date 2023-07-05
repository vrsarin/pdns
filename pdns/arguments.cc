/*
 * This file is part of PowerDNS or dnsdist.
 * Copyright -- PowerDNS.COM B.V. and its contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * In addition, for the avoidance of any doubt, permission is granted to
 * link this program with OpenSSL and to (re)distribute the binaries
 * produced as the result of such linking.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "arguments.hh"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/compare.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "namespaces.hh"
#include "logger.hh"
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <climits>

ArgvMap::param_t::const_iterator ArgvMap::begin()
{
  return d_params.begin();
}

ArgvMap::param_t::const_iterator ArgvMap::end()
{
  return d_params.end();
}

string& ArgvMap::set(const string& var)
{
  return d_params[var];
}

void ArgvMap::setDefault(const string& var, const string& value)
{
  if (defaultmap.count(var) == 0) {
    defaultmap.insert(pair<string, string>(var, value));
  }
}

void ArgvMap::setDefaults()
{
  for (const auto& param : d_params) {
    if (defaultmap.count(param.first) == 0) {
      defaultmap.insert(param);
    }
  }
}

bool ArgvMap::mustDo(const string& var)
{
  return ((*this)[var] != "no") && ((*this)[var] != "off");
}

vector<string> ArgvMap::list()
{
  vector<string> ret;
  ret.reserve(d_params.size());
  for (const auto& param : d_params) {
    ret.push_back(param.first);
  }
  return ret;
}

string& ArgvMap::set(const string& var, const string& help)
{
  helpmap[var] = help;
  d_typeMap[var] = "Parameter";
  return set(var);
}

void ArgvMap::setCmd(const string& var, const string& help)
{
  helpmap[var] = help;
  d_typeMap[var] = "Command";
  set(var) = "no";
}

string& ArgvMap::setSwitch(const string& var, const string& help)
{
  helpmap[var] = help;
  d_typeMap[var] = "Switch";
  return set(var);
}

bool ArgvMap::contains(const string& var, const string& val)
{
  const auto& param = d_params.find(var);
  if (param == d_params.end() || param->second.empty()) {
    return false;
  }
  vector<string> parts;

  stringtok(parts, param->second, ", \t");
  return std::any_of(parts.begin(), parts.end(), [&](const std::string& str) { return str == val; });
}

string ArgvMap::helpstring(string prefix)
{
  if (prefix == "no") {
    prefix = "";
  }

  string help;

  for (const auto& helpitem : helpmap) {
    if (!prefix.empty() && helpitem.first.find(prefix) != 0) { // only print items with prefix
      continue;
    }

    help += "  --";
    help += helpitem.first;

    string type = d_typeMap[helpitem.first];

    if (type == "Parameter") {
      help += "=...";
    }
    else if (type == "Switch") {
      help += " | --" + helpitem.first + "=yes";
      help += " | --" + helpitem.first + "=no";
    }

    help += "\n\t";
    help += helpitem.second;
    help += "\n";
  }
  return help;
}

string ArgvMap::formatOne(bool running, bool full, const string& var, const string& help, const string& theDefault, const string& current)
{
  string out;

  if (!running || full) {
    out += "#################################\n";
    out += "# ";
    out += var;
    out += "\t";
    out += help;
    out += "\n#\n";
  }
  else {
    if (theDefault == current) {
      return "";
    }
  }

  if (!running || theDefault == current) {
    out += "# ";
  }

  if (running) {
    out += var + "=" + current + "\n";
    if (full) {
      out += "\n";
    }
  }
  else {
    out += var + "=" + theDefault + "\n\n";
  }

  return out;
}

// If running and full, only changed settings are returned.
string ArgvMap::configstring(bool running, bool full)
{
  string help;

  if (running) {
    help = "# Autogenerated configuration file based on running instance (" + nowTime() + ")\n\n";
  }
  else {
    help = "# Autogenerated configuration file template\n\n";
  }

  // Affects parsing, should come first.
  help += formatOne(running, full, "ignore-unknown-settings", helpmap["ignore-unknown-settings"], defaultmap["ignore-unknown-settings"], d_params["ignore-unknown-settings"]);

  for (const auto& helpitem : helpmap) {
    if (d_typeMap[helpitem.first] == "Command") {
      continue;
    }
    if (helpitem.first == "ignore-unknown-settings") {
      continue;
    }

    if (defaultmap.count(helpitem.first) == 0) {
      throw ArgException(string("Default for setting '") + helpitem.first + "' not set");
    }

    help += formatOne(running, full, helpitem.first, helpitem.second, defaultmap[helpitem.first], d_params[helpitem.first]);
  }

  if (running) {
    for (const auto& unknown : d_unknownParams) {
      help += formatOne(running, full, unknown.first, "unknown setting", "", unknown.second);
    }
  }

  return help;
}

const string& ArgvMap::operator[](const string& arg)
{
  if (!parmIsset(arg)) {
    throw ArgException(string("Undefined but needed argument: '") + arg + "'");
  }

  return d_params[arg];
}

mode_t ArgvMap::asMode(const string& arg)
{
  if (!parmIsset(arg)) {
    throw ArgException(string("Undefined but needed argument: '") + arg + "'");
  }

  const auto* const cptr_orig = d_params[arg].c_str();
  char* cptr_ret = nullptr;

  auto mode = static_cast<mode_t>(strtol(cptr_orig, &cptr_ret, 8));
  if (mode == 0 && cptr_ret == cptr_orig) {
    throw ArgException("'" + arg + string("' contains invalid octal mode"));
  }
  return mode;
}

gid_t ArgvMap::asGid(const string& arg)
{
  if (!parmIsset(arg)) {
    throw ArgException(string("Undefined but needed argument: '") + arg + "'");
  }

  const auto* cptr_orig = d_params[arg].c_str();
  char* cptr_ret = nullptr;
  auto gid = static_cast<gid_t>(strtol(cptr_orig, &cptr_ret, 0));
  if (gid == 0 && cptr_ret == cptr_orig) {
    // try to resolve

    struct group* group = getgrnam(d_params[arg].c_str()); // NOLINT: called before going multi-threaded
    if (group == nullptr) {
      throw ArgException("'" + arg + string("' contains invalid group"));
    }
    gid = group->gr_gid;
  }
  return gid;
}

uid_t ArgvMap::asUid(const string& arg)
{
  if (!parmIsset(arg)) {
    throw ArgException(string("Undefined but needed argument: '") + arg + "'");
  }

  const auto* cptr_orig = d_params[arg].c_str();
  char* cptr_ret = nullptr;

  auto uid = static_cast<uid_t>(strtol(cptr_orig, &cptr_ret, 0));
  if (uid == 0 && cptr_ret == cptr_orig) {
    // try to resolve
    struct passwd* pwent = getpwnam(d_params[arg].c_str()); // NOLINT: called before going multi-threaded
    if (pwent == nullptr) {
      throw ArgException("'" + arg + string("' contains invalid group"));
    }
    uid = pwent->pw_uid;
  }
  return uid;
}

int ArgvMap::asNum(const string& arg, int def)
{
  if (!parmIsset(arg)) {
    throw ArgException(string("Undefined but needed argument: '") + arg + "'");
  }

  // use default for empty values
  if (d_params[arg].empty()) {
    return def;
  }

  const auto* cptr_orig = d_params[arg].c_str();
  char* cptr_ret = nullptr;
  auto retval = static_cast<int>(strtol(cptr_orig, &cptr_ret, 0));
  if (retval == 0 && cptr_ret == cptr_orig) {
    throw ArgException("'" + arg + "' value '" + string(cptr_orig) + string("' is not a valid number"));
  }

  return retval;
}

bool ArgvMap::isEmpty(const string& arg)
{
  if (!parmIsset(arg)) {
    return true;
  }
  return d_params[arg].empty();
}

double ArgvMap::asDouble(const string& arg)
{
  if (!parmIsset(arg)) {
    throw ArgException(string("Undefined but needed argument: '") + arg + "'");
  }

  if (d_params[arg].empty()) {
    return 0.0;
  }

  const auto* cptr_orig = d_params[arg].c_str();
  char* cptr_ret = nullptr;
  auto retval = strtod(cptr_orig, &cptr_ret);

  if (retval == 0 && cptr_ret == cptr_orig) {
    throw ArgException("'" + arg + string("' is not valid double"));
  }

  return retval;
}

ArgvMap::ArgvMap()
{
  set("ignore-unknown-settings", "Configuration settings to ignore if they are unknown") = "";
}

bool ArgvMap::parmIsset(const string& var)
{
  return d_params.find(var) != d_params.end();
}

// ATM Shared between Recursor and Auth, is that a good idea?
static const map<string, string> deprecateList = {
  {"stats-api-blacklist", "stats-api-disabled-list"},
  {"stats-carbon-blacklist", "stats-carbon-disabled-list"},
  {"stats-rec-control-blacklist", "stats-rec-control-disabled-list"},
  {"stats-snmp-blacklist", "stats-snmp-disabled-list"},
  {"edns-subnet-whitelist", "edns-subnet-allow-list"},
  {"new-domain-whitelist", "new-domain-ignore-list"},
  {"snmp-master-socket", "snmp-daemon-socket"},
  {"xpf-allow-from", "Proxy Protocol"},
  {"xpf-rr-code", "Proxy Protocol"},
};

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): accesses d_log (compiled out in auth, hence clang-tidy message)
void ArgvMap::warnIfDeprecated(const string& var) const
{
  const auto msg = deprecateList.find(var);
  if (msg != deprecateList.end()) {
    SLOG(g_log << Logger::Warning << "'" << var << "' is deprecated and will be removed in a future release, use '" << msg->second << "' instead" << endl,
         d_log->info(Logr::Warning, "Option is deprecated and will be removed in a future release", "deprecatedName", Logging::Loggable(var), "alternative", Logging::Loggable(msg->second)));
  }
}

string ArgvMap::isDeprecated(const string& var)
{
  const auto msg = deprecateList.find(var);
  return msg != deprecateList.end() ? msg->second : "";
}

void ArgvMap::parseOne(const string& arg, const string& parseOnly, bool lax)
{
  string var;
  string val;
  string::size_type pos = 0;
  bool incremental = false;

  if (arg.find("--") == 0 && (pos = arg.find("+=")) != string::npos) // this is a --port+=25 case
  {
    var = arg.substr(2, pos - 2);
    val = arg.substr(pos + 2);
    incremental = true;
  }
  else if (arg.find("--") == 0 && (pos = arg.find('=')) != string::npos) // this is a --port=25 case
  {
    var = arg.substr(2, pos - 2);
    val = arg.substr(pos + 1);
  }
  else if (arg.find("--") == 0 && (arg.find('=') == string::npos)) // this is a --daemon case
  {
    var = arg.substr(2);
    val = "";
  }
  else if (arg[0] == '-' && arg.length() > 1) {
    var = arg.substr(1);
    val = "";
  }
  else { // command
    d_cmds.push_back(arg);
  }

  boost::trim(var);

  if (!var.empty() && (parseOnly.empty() || var == parseOnly)) {
    if (!lax) {
      warnIfDeprecated(var);
    }
    pos = val.find_first_not_of(" \t"); // strip leading whitespace
    if (pos != 0 && pos != string::npos) {
      val = val.substr(pos);
    }
    if (parmIsset(var)) {
      if (incremental) {
        if (d_params[var].empty()) {
          if (d_cleared.count(var) == 0) {
            throw ArgException("Incremental setting '" + var + "' without a parent");
          }
          d_params[var] = val;
        }
        else {
          d_params[var] += ", " + val;
        }
      }
      else {
        d_params[var] = val;
        d_cleared.insert(var);
      }
    }
    else {
      // unknown setting encountered. see if its on the ignore list before throwing.
      vector<string> parts;
      stringtok(parts, d_params["ignore-unknown-settings"], " ,\t\n\r");
      if (find(parts.begin(), parts.end(), var) != parts.end()) {
        d_unknownParams[var] = val;
        SLOG(g_log << Logger::Warning << "Ignoring unknown setting '" << var << "' as requested" << endl,
             d_log->info(Logr::Warning, "Ignoring unknown setting as requested", "name", Logging::Loggable(var)));
        return;
      }

      if (!lax) {
        throw ArgException("Trying to set unknown setting '" + var + "'");
      }
    }
  }
}

const vector<string>& ArgvMap::getCommands()
{
  return d_cmds;
}

void ArgvMap::parse(int& argc, char** argv, bool lax)
{
  d_cmds.clear();
  d_cleared.clear();
  for (int i = 1; i < argc; i++) {
    parseOne(argv[i], "", lax); // NOLINT: Posix argument parsing
  }
}

void ArgvMap::preParse(int& argc, char** argv, const string& arg)
{
  for (int i = 1; i < argc; i++) {
    string varval = argv[i]; // NOLINT: Posix argument parsing
    if (varval.find("--" + arg) == 0) {
      parseOne(argv[i]); // NOLINT:  Posix argument parsing
    }
  }
}

bool ArgvMap::parseFile(const string& fname, const string& arg, bool lax)
{
  string line;
  string pline;

  std::ifstream configFileStream(fname);
  if (!configFileStream) {
    return false;
  }

  while (getline(configFileStream, pline)) {
    boost::trim_right(pline);

    if (!pline.empty() && pline[pline.size() - 1] == '\\') {
      line += pline.substr(0, pline.length() - 1);
      continue;
    }

    line += pline;

    // strip everything after a #
    string::size_type pos = line.find('#');
    if (pos != string::npos) {
      // make sure it's either first char or has whitespace before
      // fixes issue #354
      if (pos == 0 || (std::isspace(line[pos - 1]) != 0)) {
        line = line.substr(0, pos);
      }
    }

    // strip trailing spaces
    boost::trim_right(line);

    // strip leading spaces
    pos = line.find_first_not_of(" \t\r\n");
    if (pos != string::npos) {
      line = line.substr(pos);
    }

    // gpgsql-basic-query=sdfsdfs dfsdfsdf sdfsdfsfd

    parseOne(string("--") + line, arg, lax);
    line = "";
  }

  return true;
}

bool ArgvMap::preParseFile(const string& fname, const string& arg, const string& theDefault)
{
  d_params[arg] = theDefault;

  return parseFile(fname, arg, false);
}

bool ArgvMap::file(const string& fname, bool lax)
{
  return file(fname, lax, false);
}

bool ArgvMap::file(const string& fname, bool lax, bool included)
{
  if (!parmIsset("include-dir")) { // inject include-dir
    set("include-dir", "Directory to include configuration files from");
  }

  if (!parseFile(fname, "", lax)) {
    SLOG(g_log << Logger::Warning << "Unable to open " << fname << std::endl,
         d_log->error(Logr::Warning, "Unable to open file", "name", Logging::Loggable(fname)));
    return false;
  }

  // handle include here (avoid re-include)
  if (!included && !d_params["include-dir"].empty()) {
    std::vector<std::string> extraConfigs;
    gatherIncludes(d_params["include-dir"], ".conf", extraConfigs);
    for (const std::string& filename : extraConfigs) {
      if (!file(filename.c_str(), lax, true)) {
        SLOG(g_log << Logger::Error << filename << " could not be parsed" << std::endl,
             d_log->info(Logr::Error, "Unable to parse config file", "name", Logging::Loggable(filename)));
        throw ArgException(filename + " could not be parsed");
      }
    }
  }

  return true;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): accesses d_log (compiled out in auth, hence clang-tidy message)
void ArgvMap::gatherIncludes(const std::string& directory, const std::string& suffix, std::vector<std::string>& extraConfigs)
{
  if (directory.empty()) {
    return; // nothing to do
  }

  auto dir = std::unique_ptr<DIR, decltype(&closedir)>(opendir(directory.c_str()), closedir);
  if (dir == nullptr) {
    int err = errno;
    string msg = directory + " is not accessible: " + stringerror(err);
    SLOG(g_log << Logger::Error << msg << std::endl,
         d_log->error(Logr::Error, err, "Directory is not accessible", "name", Logging::Loggable(directory)));
    throw ArgException(msg);
  }

  std::vector<std::string> vec;
  struct dirent* ent = nullptr;
  while ((ent = readdir(dir.get())) != nullptr) { // NOLINT(concurrency-mt-unsafe): see Linux man page
    if (ent->d_name[0] == '.') {
      continue; // skip any dots
    }
    if (boost::ends_with(ent->d_name, suffix)) {
      // build name
      string name = directory + "/" + ent->d_name; // NOLINT: Posix API
      // ensure it's readable file
      struct stat statInfo
      {
      };
      if (stat(name.c_str(), &statInfo) != 0 || !S_ISREG(statInfo.st_mode)) {
        string msg = name + " is not a regular file";
        SLOG(g_log << Logger::Error << msg << std::endl,
             d_log->info(Logr::Error, "Unable to open non-regular file", "name", Logging::Loggable(name)));
        throw ArgException(msg);
      }
      vec.emplace_back(name);
    }
  }
  std::sort(vec.begin(), vec.end(), CIStringComparePOSIX());
  extraConfigs.insert(extraConfigs.end(), vec.begin(), vec.end());
}
