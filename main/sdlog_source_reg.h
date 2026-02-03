// SDLOG_SOURCE_REG(_name, _fd_name, _fmt)
// name: used to generate enum to specify channel
// fd_name: the folder name to store logs
// fmt: SDLOG_FMT_TEXT/ SDLOG_FMT_CAN
SDLOG_SOURCE_REG(HTTP, "http", SDLOG_FMT_TEXT)
SDLOG_SOURCE_REG(CAN, "can", SDLOG_FMT_CAN)
SDLOG_SOURCE_REG(CONSOLE, "console", SDLOG_FMT_TEXT)
