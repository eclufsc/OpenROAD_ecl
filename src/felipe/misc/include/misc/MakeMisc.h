#pragma once

namespace misc{
class Misc;
}

namespace odb{
class dbDatabase;
}

namespace ord {

class OpenRoad;

misc::Misc* makeMisc();

void initMisc(OpenRoad *openroad);

void deleteMisc(misc::Misc *misc);

}  // namespace ord
