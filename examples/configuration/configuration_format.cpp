#include "LogAsync.h"

int main(int argc, char* argv[])
{
    auto logfile = Logging::RegisterLog("LogAsync_ConfigTest.txt");

    // I'll refer you to the code to explain what exactly everything does:
    // - "ConfigurationHandler.h" -> SetLogFormat
    logfile->SetConfiguration("%t | %S | %m | %T", "%a, %b %d, %Y");

    LOG_ASYNC("Testing") << "Hey look I changed the logging format that we're using!" << std::endl;

    // Configuration changes only stick around as long as the file is in use.
    // If you need to re-create the log file and open it again, you'll also
    // need to set the LoggingFormat again!
    
    Logging::ShutdownLogging();
   
    return 0;
}