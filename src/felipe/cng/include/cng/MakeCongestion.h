#pragma once

namespace cng{
class Congestion;
}

namespace odb{
class dbDatabase;
}

namespace ord {

class OpenRoad;

cng::Congestion* makeCongestion();

void initCongestion(OpenRoad *openroad);

void deleteCongestion(cng::Congestion *congestion);

}  // namespace ord
