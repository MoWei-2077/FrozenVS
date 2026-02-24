// Freezeit 冻它模块  By JARK006

#include "freezeit.hpp"
#include "settings.hpp"
#include "managedApp.hpp"
#include "systemTools.hpp"
#include "doze.hpp"
#include "freezer.hpp"
#include "server.hpp"

int main(int argc, char **argv) {
    //先获取模块当前目录，Init()开启守护线程后, 工作目录将切换到根目录 "/"
    char fullPath[1024] = {};
    auto pathPtr = realpath(argv[0], fullPath); 

    Utils::Init();

    Freezeit freezeit(argc, string(pathPtr));
    Settings settings(freezeit);
    SystemTools systemTools(freezeit, settings);
    ManagedApp managedApp(freezeit, settings);
    Doze doze(freezeit, settings, managedApp, systemTools);
    Freezer freezer(freezeit, settings, managedApp, systemTools, doze);
    Server server(freezeit, settings, managedApp, systemTools, doze, freezer);

    sleep(3600 * 24 * 365);//放年假
    return 0;
}
