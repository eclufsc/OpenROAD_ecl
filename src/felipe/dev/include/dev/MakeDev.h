#pragma once

namespace dev{
class Dev;
}

namespace odb{
class dbDatabase;
}

namespace ord {

class OpenRoad;

dev::Dev* makeDev();

void initDev(OpenRoad *openroad);

void deleteDev(dev::Dev *dev);

}  // namespace ord

