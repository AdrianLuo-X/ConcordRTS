#pragma once
#include <iosfwd>
#include <string>
#include <vector>
class IniFile;
struct WorldObjectInstance;
namespace concord::simulation::superweapons {
bool hasSupportFlightPose(const WorldObjectInstance& object);
void writeSupportFlightPoses(const std::vector<WorldObjectInstance>& objects, std::ostream& output);
bool restoreSupportFlightPoses(const IniFile& ini, std::vector<WorldObjectInstance>& objects, std::string& error);
}
