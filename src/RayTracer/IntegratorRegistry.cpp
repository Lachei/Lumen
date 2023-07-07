#include "IntegratorRegistry.h"
#include "Path.h"
#include "BDPT.h"
#include "SPPM.h"
#include "VCM.h"
#include "PSSMLT.h"
#include "SMLT.h"
#include "VCMMLT.h"
#include "ReSTIR.h"
#include "ReSTIRGI.h"
#include "DDGI.h"
#include "LightResampling/SBDPTResampled.h"

std::map<std::string_view, IntegratorRegistry::Entry> IntegratorRegistry::integrators{};

REGISTER(Path);
REGISTER(BDPT);
REGISTER(SPPM);
REGISTER(VCM);
REGISTER(PSSMLT);
REGISTER(SMLT);
REGISTER(VCMMLT);
REGISTER(ReSTIR);
REGISTER(ReSTIRGI);
REGISTER(DDGI);
REGISTER(SBDPTResampled);
