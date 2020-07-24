// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

JET_SET_SYSTEM_PARAMETER_W
Jet_Set_System_Parameter_W
stub_jet_set_system_parameter_w
dll_jet_set_system_parameter_w
JetSetSystemParameterW

#define NT_QUERY_SYSTEM_INFORMATION(function_name) NTSTATUS JET_API function_name(SYSTEM_INFORMATION_CLASS SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength)
#pragma warning(push)
#pragma warning(disable : 4100)
NT_QUERY_SYSTEM_INFORMATION(stub_nt_query_system_information)
{
	log_print(LOG_WARNING, "NtQuerySystemInformation: Calling the stub version of this function.");
	_ASSERT(false);
	return JET_wrnNyi;
}
#pragma warning(pop)
typedef NT_QUERY_SYSTEM_INFORMATION(Nt_Query_System_Information);
Nt_Query_System_Information* dll_nt_query_system_information = stub_nt_query_system_information;
#define NtQuerySystemInformation dll_nt_query_system_information

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

#define JET_GET_DATABASE_FILE_INFO(function_name) JET_ERR JET_API function_name(JET_PCSTR szDatabaseName, void* pvResult, unsigned long cbMax, unsigned long InfoLevel)
#pragma warning(push)
#pragma warning(disable : 4100)
JET_GET_DATABASE_FILE_INFO(stub_jet_get_database_file_info)
{
	log_print(LOG_WARNING, "JetGetDatabaseFileInfo: Calling the stub version of this function.");
	_ASSERT(false);
	return JET_wrnNyi;
}
#pragma warning(pop)
typedef JET_GET_DATABASE_FILE_INFO(Jet_Get_Database_File_Info);
Jet_Get_Database_File_Info* dll_jet_get_database_file_info = stub_jet_get_database_file_info;
#define JetGetDatabaseFileInfo dll_jet_get_database_file_info

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

#define JET_GET_SYSTEM_PARAMETER_W(function_name) JET_ERR JET_API function_name(JET_INSTANCE instance, JET_SESID sesid, unsigned long paramid, JET_API_PTR* plParam, JET_PWSTR szParam, unsigned long cbMax)
#pragma warning(push)
#pragma warning(disable : 4100)
JET_GET_SYSTEM_PARAMETER_W(stub_jet_get_system_parameter_w)
{
	log_print(LOG_WARNING, "JetGetSystemParameterW: Calling the stub version of this function.");
	_ASSERT(false);
	return JET_wrnNyi;
}
#pragma warning(pop)
typedef JET_GET_SYSTEM_PARAMETER_W(Jet_Get_System_Parameter_W);
Jet_Get_System_Parameter_W* dll_jet_get_system_parameter_w = stub_jet_get_system_parameter_w;
#define JetGetSystemParameterW dll_jet_get_system_parameter_w

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

#define JET_SET_SYSTEM_PARAMETER_W(function_name) JET_ERR JET_API function_name(JET_INSTANCE* pinstance, JET_SESID sesid, unsigned long paramid, JET_API_PTR lParam, JET_PCWSTR szParam)
#pragma warning(push)
#pragma warning(disable : 4100)
JET_SET_SYSTEM_PARAMETER_W(stub_jet_set_system_parameter_w)
{
	log_print(LOG_WARNING, "JetSetSystemParameterW: Calling the stub version of this function.");
	_ASSERT(false);
	return JET_wrnNyi;
}
#pragma warning(pop)
typedef JET_SET_SYSTEM_PARAMETER_W(Jet_Set_System_Parameter_W);
Jet_Set_System_Parameter_W* dll_jet_set_system_parameter_w = stub_jet_set_system_parameter_w;
#define JetSetSystemParameterW dll_jet_set_system_parameter_w

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

#define JET_CREATE_INSTANCE_W(function_name) JET_ERR JET_API function_name(JET_INSTANCE* pinstance, JET_PCWSTR szInstanceName)
#pragma warning(push)
#pragma warning(disable : 4100)
JET_CREATE_INSTANCE_W(stub_jet_create_instance_w)
{
	log_print(LOG_WARNING, "JetCreateInstanceW: Calling the stub version of this function.");
	_ASSERT(false);
	return JET_wrnNyi;
}
#pragma warning(pop)
typedef JET_CREATE_INSTANCE_W(Jet_Create_Instance_W);
Jet_Create_Instance_W* dll_jet_create_instance_w = stub_jet_create_instance_w;
#define JetCreateInstanceW dll_jet_create_instance_w

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

#define JET_INIT(function_name) JET_ERR JET_API function_name(JET_INSTANCE* pinstance)
#pragma warning(push)
#pragma warning(disable : 4100)
JET_INIT(stub_jet_init)
{
	log_print(LOG_WARNING, "JetInit: Calling the stub version of this function.");
	_ASSERT(false);
	return JET_wrnNyi;
}
#pragma warning(pop)
typedef JET_INIT(Jet_Init);
Jet_Init* dll_jet_init = stub_jet_init;
#define JetInit dll_jet_init

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

#define JET_TERM(function_name) JET_ERR JET_API function_name(JET_INSTANCE instance)
#pragma warning(push)
#pragma warning(disable : 4100)
JET_TERM(stub_jet_term)
{
	log_print(LOG_WARNING, "JetTerm: Calling the stub version of this function.");
	_ASSERT(false);
	return JET_wrnNyi;
}
#pragma warning(pop)
typedef JET_TERM(Jet_Term);
Jet_Term* dll_jet_term = stub_jet_term;
#define JetTerm dll_jet_term

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

#define JET_BEGIN_SESSION_W(function_name) JET_ERR JET_API function_name(JET_INSTANCE instance, JET_SESID* psesid, JET_PCWSTR szUserName, JET_PCWSTR szPassword)
#pragma warning(push)
#pragma warning(disable : 4100)
JET_BEGIN_SESSION_W(stub_jet_begin_session_w)
{
	log_print(LOG_WARNING, "JetBeginSessionW: Calling the stub version of this function.");
	_ASSERT(false);
	return JET_wrnNyi;
}
#pragma warning(pop)
typedef JET_BEGIN_SESSION_W(Jet_Begin_Session_W);
Jet_Begin_Session_W* dll_jet_begin_session_w = stub_jet_begin_session_w;
#define JetBeginSessionW dll_jet_begin_session_w

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

#define JET_END_SESSION(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_GRBIT grbit)
#pragma warning(push)
#pragma warning(disable : 4100)
JET_END_SESSION(stub_jet_end_session)
{
	log_print(LOG_WARNING, "JetEndSession: Calling the stub version of this function.");
	_ASSERT(false);
	return JET_wrnNyi;
}
#pragma warning(pop)
typedef JET_END_SESSION(Jet_End_Session);
Jet_End_Session* dll_jet_end_session = stub_jet_end_session;
#define JetEndSession dll_jet_end_session

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

#define JET_ATTACH_DATABASE_2_W(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_PCWSTR szFilename, const unsigned long cpgDatabaseSizeMax, JET_GRBIT grbit)
#pragma warning(push)
#pragma warning(disable : 4100)
JET_ATTACH_DATABASE_2_W(stub_jet_attach_database_2_w)
{
	log_print(LOG_WARNING, "JetAttachDatabase2W: Calling the stub version of this function.");
	_ASSERT(false);
	return JET_wrnNyi;
}
#pragma warning(pop)
typedef JET_ATTACH_DATABASE_2_W(Jet_Attach_Database_2_W);
Jet_Attach_Database_2_W* dll_jet_attach_database_2_w = stub_jet_attach_database_2_w;
#define JetAttachDatabase2W dll_jet_attach_database_2_w

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

#define JET_DETACH_DATABASE_W(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_PCWSTR szFilename)
#pragma warning(push)
#pragma warning(disable : 4100)
JET_DETACH_DATABASE_W(stub_jet_detach_database_w)
{
	log_print(LOG_WARNING, "JetDetachDatabaseW: Calling the stub version of this function.");
	_ASSERT(false);
	return JET_wrnNyi;
}
#pragma warning(pop)
typedef JET_DETACH_DATABASE_W(Jet_Detach_Database_W);
Jet_Detach_Database_W* dll_jet_detach_database_w = stub_jet_detach_database_w;
#define JetDetachDatabaseW dll_jet_detach_database_w

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

#define JET_OPEN_DATABASE_W(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_PCWSTR szFilename, JET_PCWSTR szConnect, JET_DBID* pdbid, JET_GRBIT grbit)
#pragma warning(push)
#pragma warning(disable : 4100)
JET_OPEN_DATABASE_W(stub_jet_open_database_w)
{
	log_print(LOG_WARNING, "JetOpenDatabaseW: Calling the stub version of this function.");
	_ASSERT(false);
	return JET_wrnNyi;
}
#pragma warning(pop)
typedef JET_OPEN_DATABASE_W(Jet_Open_Database_W);
Jet_Open_Database_W* dll_jet_open_database_w = stub_jet_open_database_w;
#define JetOpenDatabaseW dll_jet_open_database_w

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

#define JET_CLOSE_DATABASE(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_DBID dbid, JET_GRBIT grbit)
#pragma warning(push)
#pragma warning(disable : 4100)
JET_CLOSE_DATABASE(stub_jet_close_database)
{
	log_print(LOG_WARNING, "JetCloseDatabase: Calling the stub version of this function.");
	_ASSERT(false);
	return JET_wrnNyi;
}
#pragma warning(pop)
typedef JET_CLOSE_DATABASE(Jet_Close_Database);
Jet_Close_Database* dll_jet_close_database = stub_jet_close_database;
#define JetCloseDatabase dll_jet_close_database

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

JetOpenTableW
JetCloseTable

JetGetTableColumnInfoW
JetRetrieveColumn
JetRetrieveColumns
JetGetRecordPosition
JetMove
