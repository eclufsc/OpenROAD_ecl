#pragma once

namespace leg{
class Legalizer;
}

namespace odb{
class dbDatabase;
}

namespace ord {

class OpenRoad;

leg::Legalizer* makeLegalizer();

void initLegalizer(OpenRoad *openroad);

void deleteLegalizer(leg::Legalizer *legalizer);

}  // namespace ord

