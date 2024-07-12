#pragma once

namespace feps_subs{
class FepsSubstitute;
}

namespace odb{
class dbDatabase;
}

namespace ord {

class OpenRoad;

feps_subs::FepsSubstitute* makeFepsSubstitute();

void initFepsSubstitute(OpenRoad *openroad);

void deleteFepsSubstitute(feps_subs::FepsSubstitute *feps_substitute);

}  // namespace ord

