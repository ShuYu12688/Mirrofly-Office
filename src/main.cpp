#include <mirrorfly/app.hpp>

int main(int argc, char* argv[])
{
    mirrorfly_configure();
    return mirrorfly_run(argc, argv);
}
