#include <mirrorfly/app.hpp>
#include <mirrorfly/ui.hpp>

void mirrorfly_configure()
{
    mirrorfly::configure_interface();
}

int mirrorfly_run(int argc, char* argv[])
{
    return mirrorfly::run_interface(argc, argv);
}
