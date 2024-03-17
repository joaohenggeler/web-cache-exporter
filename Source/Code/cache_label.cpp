#include "cache.h"
#include "common.h"

static bool label_load(Exporter* exporter, String* path)
{
	ASSERT(exporter->labels != NULL, "Missing labels");

	bool success = true;
	String* content = NULL;

	TO_TEMPORARY_ARENA()
	{
		File file = {};
		success = file_read_all(path, &file);
		content = string_from_utf_8((char*) file.data);
	}

	int previous_count = exporter->labels->count;

	if(success)
	{
		enum List_Type
		{
			LIST_NONE,
			LIST_SIGNATURES,
			LIST_MIME_TYPES,
			LIST_EXTENSIONS,
			LIST_DOMAINS,
		};

		String* major_name = EMPTY_STRING;
		Label label = {};
		label.type = LABEL_NONE;
		List_Type list_type = LIST_NONE;

		Split_State line_state = {};
		line_state.str = content;
		line_state.delimiters = LINE_DELIMITERS;

		String_View line = {};
		while(string_split(&line_state, &line))
		{
			line = string_trim(line);
			if(string_begins_with(line, T("#"))) continue;

			if(string_is_equal(line, T("END")))
			{
				if(list_type != LIST_NONE)
				{
					list_type = LIST_NONE;
					continue;
				}
				else if(label.type != LABEL_NONE)
				{
					array_add(&exporter->labels, label);
					ZeroMemory(&label, sizeof(label));
					label.type = LABEL_NONE;
					continue;
				}
				else
				{
					log_error("Unexpected END directive in '%s'", path->data);
					success = false;
					break;
				}
			}

			if(label.type != LABEL_NONE)
			{
				if(label.type == LABEL_FILE)
				{
					if(list_type != LIST_NONE)
					{
						if(list_type == LIST_SIGNATURES)
						{
							ASSERT(label.signatures != NULL, "Missing signatures");

							Array<u8>* bytes = array_create<u8>(0);

							{
								Split_State signature_state = {};
								signature_state.view = line;
								signature_state.delimiters = SPACE_DELIMITERS;

								String_View value = {};
								while(string_split(&signature_state, &value))
								{
									u32 byte = 0;

									if(string_is_equal(value, T("__")))
									{
										array_add(&bytes, (u8) 0);
									}
									else if(string_hex_to_num(value, &byte) && byte <= 0xFF)
									{
										array_add(&bytes, (u8) byte);
									}
									else
									{
										log_error("Found invalid signature byte '%.*s' in '%s'", value.code_count, value.data, path->data);
										goto continue_outer;
									}
								}
							}

							Array<bool>* wildcards = array_create<bool>(bytes->count);

							{
								Split_State signature_state = {};
								signature_state.view = line;
								signature_state.delimiters = SPACE_DELIMITERS;

								String_View value = {};
								while(string_split(&signature_state, &value))
								{
									bool wildcard = string_is_equal(value, T("__"));
									array_add(&wildcards, wildcard);
								}
							}

							ASSERT(bytes->count == wildcards->count, "Mismatched bytes and wildcards");

							Signature signature = {};
							signature.bytes = bytes;
							signature.wildcards = wildcards;
							array_add(&label.signatures, signature);

							exporter->max_signature_size = MAX(exporter->max_signature_size, (size_t) bytes->count);
						}
						else if(list_type == LIST_MIME_TYPES)
						{
							ASSERT(label.mime_types != NULL, "Missing MIME types");
							String* mime_type = string_from_view(line);
							array_add(&label.mime_types, mime_type);
						}
						else if(list_type == LIST_EXTENSIONS)
						{
							ASSERT(label.extensions != NULL, "Missing extensions");

							Split_State extension_state = {};
							extension_state.view = line;
							extension_state.delimiters = SPACE_DELIMITERS;

							String_View extension = {};
							while(string_split(&extension_state, &extension))
							{
								array_add(&label.extensions, string_from_view(extension));
							}
						}
						else
						{
							ASSERT(false, "Unhandled file list type");
						}
					}
					else
					{
						if(string_is_equal(line, T("BEGIN_SIGNATURES")))
						{
							list_type = LIST_SIGNATURES;
							if(label.signatures == NULL) label.signatures = array_create<Signature>(4);
						}
						else if(string_is_equal(line, T("BEGIN_MIME_TYPES")))
						{
							list_type = LIST_MIME_TYPES;
							if(label.mime_types == NULL) label.mime_types = array_create<String*>(4);
						}
						else if(string_is_equal(line, T("BEGIN_EXTENSIONS")))
						{
							list_type = LIST_EXTENSIONS;
							if(label.extensions == NULL) label.extensions = array_create<String*>(4);
						}
						else
						{
							Split_State directive_state = {};
							directive_state.view = line;
							directive_state.delimiters = SPACE_DELIMITERS;

							String_View directive = {};
							String_View value = {};
							bool split = string_partition(&directive_state, &directive, &value);

							if(string_is_equal(directive, T("DEFAULT_EXTENSION")))
							{
								if(split)
								{
									value = string_trim(value);
									label.default_extension = string_from_view(value);
								}
								else
								{
									log_error("Missing the default extension value in file label '%s' in '%s'", label.minor_name->data, path->data);
									success = false;
									break;
								}
							}
							else
							{
								log_error("Unknown directive '%.*s' in file label '%s' in '%s'", directive.code_count, directive.data, label.minor_name->data, path->data);
								success = false;
								break;
							}
						}
					}
				}
				else if(label.type == LABEL_URL)
				{
					if(list_type != LIST_NONE)
					{
						if(list_type == LIST_DOMAINS)
						{
							ASSERT(label.domains != NULL, "Missing domains");

							Split_State domain_state = {};
							domain_state.view = line;
							domain_state.delimiters = T("/");

							String_View host = {};
							String_View path = {};
							string_partition(&domain_state, &host, &path);

							Domain domain = {};
							domain.host = string_from_view(host);
							domain.path = string_from_view(path);

							array_add(&label.domains, domain);
						}
						else
						{
							ASSERT(false, "Unhandled URL list type");
						}
					}
					else
					{
						if(string_is_equal(line, T("BEGIN_DOMAINS")))
						{
							list_type = LIST_DOMAINS;
							if(label.domains == NULL) label.domains = array_create<Domain>(4);
						}
						else
						{
							log_error("Unknown directive '%.*s' in URL label '%s' in '%s'", line.code_count, line.data, label.minor_name->data, path->data);
							success = false;
							break;
						}
					}
				}
				else
				{
					ASSERT(false, "Unhandled label type");
				}
			}
			else
			{
				Split_State directive_state = {};
				directive_state.view = line;
				directive_state.delimiters = SPACE_DELIMITERS;

				String_View directive = {};
				String_View value = {};

				if(!string_partition(&directive_state, &directive, &value))
				{
					log_error("Missing the value in directive '%.*s' in '%s'", directive.code_count, directive.data, path->data);
					success = false;
					break;
				}

				value = string_trim(value);

				if(string_is_equal(directive, T("NAME")))
				{
					major_name = string_from_view(value);
				}
				else if(string_is_equal(directive, T("BEGIN_FILE")))
				{
					label.type = LABEL_FILE;
					label.minor_name = string_from_view(value);
				}
				else if(string_is_equal(directive, T("BEGIN_URL")))
				{
					label.type = LABEL_URL;
					label.minor_name = string_from_view(value);
				}
				else
				{
					log_error("Unknown directive '%.*s' in '%s'", directive.code_count, directive.data, path->data);
					success = false;
					break;
				}
			}

			continue_outer:;
		}

		if(success && list_type != LIST_NONE)
		{
			log_error("Unterminated list in '%s'", path->data);
			success = false;
		}

		if(success && label.type != LABEL_NONE)
		{
			log_error("Unterminated label in '%s'", path->data);
			success = false;
		}

		if(success)
		{
			for(int i = previous_count; i < exporter->labels->count; i += 1)
			{
				exporter->labels->data[i].major_name = major_name;
			}

			if(exporter->labels->count == previous_count)
			{
				log_warning("No labels found in '%s'", path->data);
			}
		}
		else
		{
			array_truncate(exporter->labels, previous_count);
		}
	}
	else
	{
		log_error("Failed to read '%s'", path->data);
		success = false;
	}

	return success;
}

void label_load_all(Exporter* exporter)
{
	String* labels_path = path_build(CANY(context.executable_path), CANY(T("Labels")));

	Walk_State state = {};
	state.base_path = labels_path;
	state.query = T("*");
	state.files = true;

	Array<Walk_Info>* paths = walk_all(&state, SORT_PATHS);
	exporter->labels = array_create<Label>(paths->count * 20);

	for(int i = 0; i < paths->count; i += 1)
	{
		Walk_Info info = paths->data[i];
		int previous_count = exporter->labels->count;

		// @Future: use an arena savepoint to clear the file from memory after loading.
		if(label_load(exporter, info.path))
		{
			int loaded_count = exporter->labels->count - previous_count;
			log_info("Loaded %d labels from '%s'", loaded_count, info.path->data);
		}
		else
		{
			log_error("Failed to load '%s'", info.path->data);
		}
	}
}

void label_filter_check(Exporter* exporter)
{
	ASSERT(exporter->labels != NULL, "Missing labels");

	Array<String*>* filters[] = {exporter->positive_filter, exporter->negative_filter};

	for(int i = 0; i < _countof(filters); i += 1)
	{
		Array<String*>* filter = filters[i];
		if(filter == NULL) continue;

		for(int j = 0; j < filter->count; j += 1)
		{
			String* name = filter->data[j];

			bool found = false;

			for(int k = 0; k < exporter->labels->count; k += 1)
			{
				Label label = exporter->labels->data[k];
				if(string_is_equal(label.major_name, name, IGNORE_CASE)
				|| string_is_equal(label.minor_name, name, IGNORE_CASE))
				{
					found = true;
					break;
				}
			}

			if(!found)
			{
				console_warning("Could not find the filter name '%s' in the loaded labels", name->data);
				log_warning("Could not find the filter name '%s' in the loaded labels", name->data);
			}
		}
	}
}

bool label_file_match(Exporter* exporter, Match_Params params, Label* result)
{
	ASSERT(exporter->labels != NULL, "Missing labels");
	ASSERT(params.path != NULL, "Missing path");
	ASSERT(params.extension != NULL, "Missing extension");

	bool success = false;

	if(!path_is_equal(params.path, NO_PATH))
	{
		u8* buffer = arena_push_buffer(context.current_arena, exporter->max_signature_size, u8);
		size_t bytes_read = 0;

		if(file_read_first_at_most(params.path, buffer, exporter->max_signature_size, &bytes_read, params.temporary))
		{
			if(bytes_read > 0)
			{
				for(int i = 0; !success && i < exporter->labels->count; i += 1)
				{
					Label label = exporter->labels->data[i];
					if(label.type != LABEL_FILE || label.signatures == NULL) continue;

					for(int j = 0; j < label.signatures->count; j += 1)
					{
						Signature signature = label.signatures->data[j];
						if(signature.bytes->count > (int) bytes_read) continue;

						bool match = true;
						int count = MIN(signature.bytes->count, (int) bytes_read);

						for(int k = 0; k < count; k += 1)
						{
							u8 byte = signature.bytes->data[k];
							bool wildcard = signature.wildcards->data[k];

							if(!wildcard && byte != buffer[k])
							{
								match = false;
								break;
							}
						}

						if(match)
						{
							success = true;
							*result = label;
							break;
						}
					}
				}
			}
		}
		else
		{
			log_error("Failed to read the signature from '%s'", params.path->data);
		}
	}

	if(params.mime_type != NULL)
	{
		for(int i = 0; !success && i < exporter->labels->count; i += 1)
		{
			Label label = exporter->labels->data[i];
			if(label.type != LABEL_FILE || label.mime_types == NULL) continue;

			for(int j = 0; j < label.mime_types->count; j += 1)
			{
				String* mime_type = label.mime_types->data[j];
				if(string_begins_with(params.mime_type, mime_type, IGNORE_CASE))
				{
					success = true;
					*result = label;
					break;
				}
			}
		}
	}

	for(int i = 0; !success && i < exporter->labels->count; i += 1)
	{
		Label label = exporter->labels->data[i];
		if(label.type != LABEL_FILE || label.extensions == NULL) continue;

		for(int j = 0; j < label.extensions->count; j += 1)
		{
			String* extension = label.extensions->data[j];
			if(string_is_equal(params.extension, extension, IGNORE_CASE))
			{
				success = true;
				*result = label;
				break;
			}
		}
	}

	return success;
}

bool label_url_match(Exporter* exporter, Match_Params params, Label* result)
{
	ASSERT(exporter->labels != NULL, "Missing labels");
	ASSERT(params.url.full != NULL, "Missing URL");

	bool success = false;

	String_View url_path = string_remove_prefix(params.url.path, T("/"));

	ARENA_SAVEPOINT()
	{
		Split_State param_state = {};
		param_state.str = params.url.host;
		param_state.delimiters = T(".");
		param_state.reverse = true;

		Array<String_View>* param_components = string_split_all(&param_state);

		for(int i = 0; !success && i < exporter->labels->count; i += 1)
		{
			Label label = exporter->labels->data[i];
			if(label.type != LABEL_URL || label.domains == NULL) continue;

			for(int j = 0; j < label.domains->count; j += 1)
			{
				Domain domain = label.domains->data[j];
				bool any_tld = string_ends_with(domain.host, T(".*"));

				Split_State label_state = {};
				label_state.str = domain.host;
				label_state.delimiters = T(".");
				label_state.reverse = true;

				Array<String_View>* label_components = string_split_all(&label_state);

				if(label_components->count > param_components->count) continue;

				bool host_ok = true;

				// We'll use the label component count so we can match subdomains from the parameter host.
				for(int k = 0; k < label_components->count; k += 1)
				{
					String_View param_component = param_components->data[k];
					String_View label_component = label_components->data[k];
					bool wildcard = string_is_equal(label_component, T("*")) && k == 0;

					if(!wildcard && !string_is_equal(param_component, label_component))
					{
						host_ok = false;
						break;
					}
				}

				if(any_tld && !host_ok)
				{
					array_insert(&label_components, 0, CVIEW("*"));

					if(label_components->count <= param_components->count)
					{
						host_ok = true;

						for(int k = 0; k < label_components->count; k += 1)
						{
							String_View param_component = param_components->data[k];
							String_View label_component = label_components->data[k];
							bool wildcard = string_is_equal(label_component, T("*")) && k <= 1;

							if(!wildcard && !string_is_equal(param_component, label_component))
							{
								host_ok = false;
								break;
							}
						}
					}
				}

				bool path_ok = string_begins_with(url_path, domain.path, IGNORE_CASE);

				if(host_ok && path_ok)
				{
					success = true;
					*result = label;
					break;
				}
			}
		}
	}

	return success;
}

void label_tests(void)
{
	console_info("Running label tests");
	log_info("Running label tests");

	{
		bool success = false;
		Exporter exporter = {};
		exporter.labels = array_create<Label>(0);

		success = label_load(&exporter, CSTR("Tests\\Label\\correct.txt"));
		TEST(success, true);
		TEST(exporter.labels->count, 8);

		TEST(exporter.labels->data[0].major_name, T("Name 4"));
		TEST(exporter.labels->data[0].type, LABEL_FILE);
		TEST(exporter.labels->data[0].minor_name, T("File 1"));
		TEST(exporter.labels->data[0].signatures->count, 2);
		TEST(exporter.labels->data[0].mime_types->count, 2);
		TEST(exporter.labels->data[0].extensions->count, 6);
		TEST(exporter.labels->data[0].default_extension, T("abc"));

		TEST(exporter.labels->data[1].major_name, T("Name 4"));
		TEST(exporter.labels->data[1].type, LABEL_FILE);
		TEST(exporter.labels->data[1].minor_name, T("File 2"));
		TEST(exporter.labels->data[1].signatures->count, 2);
		TEST(exporter.labels->data[1].mime_types->count, 2);
		TEST(exporter.labels->data[1].extensions->count, 6);
		TEST(exporter.labels->data[1].default_extension, T("def"));

		TEST(exporter.labels->data[2].major_name, T("Name 4"));
		TEST(exporter.labels->data[2].type, LABEL_FILE);
		TEST(exporter.labels->data[2].minor_name, T("File 3"));
		TEST(exporter.labels->data[2].signatures->count, 0);
		TEST(exporter.labels->data[2].mime_types->count, 0);
		TEST(exporter.labels->data[2].extensions->count, 0);
		TEST((void*) exporter.labels->data[2].default_extension, NULL);

		TEST(exporter.labels->data[3].major_name, T("Name 4"));
		TEST(exporter.labels->data[3].type, LABEL_FILE);
		TEST(exporter.labels->data[3].minor_name, T("File 4"));
		TEST(exporter.labels->data[3].signatures, NULL);
		TEST(exporter.labels->data[3].mime_types, NULL);
		TEST(exporter.labels->data[3].extensions, NULL);
		TEST((void*) exporter.labels->data[3].default_extension, NULL);

		TEST(exporter.labels->data[4].major_name, T("Name 4"));
		TEST(exporter.labels->data[4].type, LABEL_URL);
		TEST(exporter.labels->data[4].minor_name, T("URL 1"));
		TEST(exporter.labels->data[4].domains->count, 4);

		TEST(exporter.labels->data[5].major_name, T("Name 4"));
		TEST(exporter.labels->data[5].type, LABEL_URL);
		TEST(exporter.labels->data[5].minor_name, T("URL 2"));
		TEST(exporter.labels->data[5].domains->count, 4);

		TEST(exporter.labels->data[6].major_name, T("Name 4"));
		TEST(exporter.labels->data[6].type, LABEL_URL);
		TEST(exporter.labels->data[6].minor_name, T("URL 3"));
		TEST(exporter.labels->data[6].domains->count, 0);

		TEST(exporter.labels->data[7].major_name, T("Name 4"));
		TEST(exporter.labels->data[7].type, LABEL_URL);
		TEST(exporter.labels->data[7].minor_name, T("URL 4"));
		TEST(exporter.labels->data[7].domains, NULL);

		success = label_load(&exporter, CSTR("Tests\\Label\\bad_signature_bytes.txt"));
		TEST(success, true);
		TEST(exporter.labels->count, 9);

		TEST(exporter.labels->data[8].major_name, T(""));
		TEST(exporter.labels->data[8].type, LABEL_FILE);
		TEST(exporter.labels->data[8].minor_name, T("File"));
		TEST(exporter.labels->data[8].signatures->count, 1);
		TEST(exporter.labels->data[8].mime_types, NULL);
		TEST(exporter.labels->data[8].extensions, NULL);
		TEST((void*) exporter.labels->data[8].default_extension, NULL);

		success = label_load(&exporter, CSTR("Tests\\Label\\empty.txt"));
		TEST(success, true);
		TEST(exporter.labels->count, 9);

		success = label_load(&exporter, CSTR("Tests\\Label\\bad_directive.txt"));
		TEST(success, false);

		success = label_load(&exporter, CSTR("Tests\\Label\\bad_file_directive.txt"));
		TEST(success, false);

		success = label_load(&exporter, CSTR("Tests\\Label\\bad_url_directive.txt"));
		TEST(success, false);

		success = label_load(&exporter, CSTR("Tests\\Label\\missing_default_extension_value.txt"));
		TEST(success, false);

		success = label_load(&exporter, CSTR("Tests\\Label\\unexpected_end.txt"));
		TEST(success, false);

		success = label_load(&exporter, CSTR("Tests\\Label\\unterminated_label.txt"));
		TEST(success, false);

		success = label_load(&exporter, CSTR("Tests\\Label\\unterminated_list.txt"));
		TEST(success, false);

		TEST(exporter.labels->count, 9);
	}

	{
		bool success = false;
		Exporter exporter = {};
		exporter.labels = array_create<Label>(0);

		success = label_load(&exporter, CSTR("Tests\\Label\\match_test.txt"));
		TEST(success, true);
		TEST(exporter.labels->count, 6);

		Match_Params params = {};
		Label label = {};

		params.extension = CSTR("wrong");

		params.path = CSTR("Tests\\Label\\match_empty");
		TEST(label_file_match(&exporter, params, &label), false);

		params.path = CSTR("Tests\\Label\\match_signature_1");
		TEST(label_file_match(&exporter, params, &label), true);
		TEST(label.type, LABEL_FILE);
		TEST(label.minor_name, T("File 1"));

		params.path = CSTR("Tests\\Label\\match_signature_2");
		TEST(label_file_match(&exporter, params, &label), true);
		TEST(label.type, LABEL_FILE);
		TEST(label.minor_name, T("File 1"));

		params.path = CSTR("Tests\\Label\\match_signature_1");
		params.mime_type = CSTR("def");
		TEST(label_file_match(&exporter, params, &label), true);
		TEST(label.type, LABEL_FILE);
		TEST(label.minor_name, T("File 1"));

		params.path = NO_PATH;
		params.mime_type = CSTR("abc");
		TEST(label_file_match(&exporter, params, &label), true);
		TEST(label.type, LABEL_FILE);
		TEST(label.minor_name, T("File 1"));

		params.path = CSTR("Tests\\Label\\match_empty");
		params.mime_type = CSTR("def");
		TEST(label_file_match(&exporter, params, &label), true);
		TEST(label.type, LABEL_FILE);
		TEST(label.minor_name, T("File 2"));

		params.path = CSTR("Tests\\Label\\match_signature_3");
		params.mime_type = CSTR("abc");
		params.extension = CSTR("a");
		TEST(label_file_match(&exporter, params, &label), true);
		TEST(label.type, LABEL_FILE);
		TEST(label.minor_name, T("File 2"));

		params.path = CSTR("Tests\\Label\\match_empty");
		params.mime_type = NULL;
		params.extension = CSTR("f");
		TEST(label_file_match(&exporter, params, &label), true);
		TEST(label.type, LABEL_FILE);
		TEST(label.minor_name, T("File 2"));

		params.url = url_parse(CSTR("http://www.wrong.com/index.html"));
		TEST(label_url_match(&exporter, params, &label), false);

		params.url = url_parse(CSTR("http://www.abc.com/index.html"));
		TEST(label_url_match(&exporter, params, &label), true);
		TEST(label.type, LABEL_URL);
		TEST(label.minor_name, T("URL 1"));

		params.url = url_parse(CSTR("http://www.abc.net/index.html"));
		TEST(label_url_match(&exporter, params, &label), true);
		TEST(label.type, LABEL_URL);
		TEST(label.minor_name, T("URL 2"));

		params.url = url_parse(CSTR("http://www.abc.co.uk/index.html"));
		TEST(label_url_match(&exporter, params, &label), true);
		TEST(label.type, LABEL_URL);
		TEST(label.minor_name, T("URL 2"));

		params.url = url_parse(CSTR("http://www.def.com/path/index.html"));
		TEST(label_url_match(&exporter, params, &label), true);
		TEST(label.type, LABEL_URL);
		TEST(label.minor_name, T("URL 3"));

		params.url = url_parse(CSTR("http://www.def.net/path/index.html"));
		TEST(label_url_match(&exporter, params, &label), true);
		TEST(label.type, LABEL_URL);
		TEST(label.minor_name, T("URL 4"));

		params.url = url_parse(CSTR("http://www.def.co.uk/path/index.html"));
		TEST(label_url_match(&exporter, params, &label), true);
		TEST(label.type, LABEL_URL);
		TEST(label.minor_name, T("URL 4"));

		params.url = url_parse(CSTR("http://wrong.com/path/index.html"));
		TEST(label_url_match(&exporter, params, &label), false);
	}
}