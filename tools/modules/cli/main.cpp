// Standalone `rensga-modules`: the same commands as `rensga modules`, without the editor and pipeline (small, fast to build in CI).
#include "modules_cmd.h"

int main(int argc, char** argv) { return runModulesCommand(argc - 1, argv + 1); }
