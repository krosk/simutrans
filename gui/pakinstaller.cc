/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "../utils/cbuffer_t.h"

#include "pakinstaller.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"
#include "../sys/simsys.h"

#include "../paksetinfo.h"

bool pakinstaller_t::finish_install;

pakinstaller_t::pakinstaller_t() :
	gui_frame_t(translator::translate("Install graphits")),
	paks(gui_scrolled_list_t::listskin),
	obsolete_paks(gui_scrolled_list_t::listskin)
{
	finish_install = false;

	set_table_layout(1, 0);

	new_component<gui_label_t>( "Select one or more graphics to install (Ctrl+click):" );

	for (int i = 0; i < 10; i++) {
		paks.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(pakinfo[i*2+1], i<10?SYSCOL_TEXT: SYSCOL_TEXT_SHADOW);
	}
	paks.enable_multiple_selection();
	scr_coord_val paks_h = paks.get_max_size().h;
	add_component(&paks);

	new_component<gui_label_t>( "The following graphics are unmaintained:" );

	for (int i = 10; i < PAKSET_COUNT; i++) {
		obsolete_paks.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(pakinfo[i*2+1], i<10?SYSCOL_TEXT: SYSCOL_TEXT_SHADOW);
	}
	obsolete_paks.enable_multiple_selection();
	scr_coord_val obsolete_paks_h = obsolete_paks.get_max_size().h;
	add_component(&obsolete_paks);

	install.init(button_t::roundbox_state, "Install");
	add_component(&install);
	install.add_listener(this);

	reset_min_windowsize();
	set_windowsize(get_min_windowsize()+scr_size(0,paks_h-paks.get_size().h + obsolete_paks_h- obsolete_paks.get_size().h));
}


/**
 * This method is called if an action is triggered
 */
#ifndef __ANDROID__
bool pakinstaller_t::action_triggered(gui_action_creator_t*, value_t)
{
	// now install
	dr_chdir( env_t::data_dir );
	FOR(vector_tpl<sint32>, i, paks.get_selections()) {
		cbuffer_t param;
#ifdef _WIN32
		param.append( "powershell .\\get_pak.ps1 \"" );
#else
		param.append( "./get_pak.sh \"" );
#endif
		param.append(pakinfo[i*2]);
		param.append("\"");

		const int retval = system( param );
		dbg->debug("pakinstaller_t::action_triggered", "Command '%s' returned %d", param.get_str(), retval);
	}

	FOR(vector_tpl<sint32>, i, obsolete_paks.get_selections()) {
		cbuffer_t param;
#ifdef _WIN32
		param.append( "powershell .\\get_pak.ps1 \"" );
#else
		param.append( "./get_pak.sh \"" );
#endif
		param.append(pakinfo[i*2+20]);
		param.append("\"");

		const int retval = system( param );
		dbg->debug("pakinstaller_t::action_triggered", "Command '%s' returned %d", param.get_str(), retval);
	}

	finish_install = true;
	return false;
}
#else
#include <curl/curl.h>
#include <zip.h>
#include <fstream>

 // linux/android specific, function to create folder makes use of opendir (linux system call) and mkdir (via system)
#include <dirent.h>
#include <errno.h>

static bool create_folder_if_required(const char* extracted_path) {
	char extracted_folder_name[FILENAME_MAX];
	strcpy(extracted_folder_name, extracted_path);
	char * last_occurence = strrchr(extracted_folder_name, '/');
	if (last_occurence != NULL) {
		last_occurence[0] = '\0';
	}
	else {
		dbg->debug(__FUNCTION__, "Error searching for path? %s", extracted_folder_name);
		return false;
	}

	DIR* dir = opendir(extracted_folder_name);
	if (dir) {
		closedir(dir);
	}
	else if (ENOENT == errno) {
		cbuffer_t param;
		param.append("mkdir -p ");
		param.append(extracted_folder_name);
		const int retval = system( param );
		dbg->debug(__FUNCTION__, "- - Created folder %s, ret %d", extracted_folder_name, retval);
	}
	else {
		dbg->debug(__FUNCTION__, "Error checking directory");
		return false;
	}

	return true;
}

static void read_zip(const char* outfilename) {
	zip_t * zip_archive;
	int err;

	if ((zip_archive = zip_open(outfilename, ZIP_RDONLY, &err)) == NULL ) {
		zip_error_t error;
		zip_error_init_with_code(&error, err);
		dbg->debug(__FUNCTION__, "cannot open zip archive: %s", zip_error_strerror(&error));
		zip_error_fini(&error);
	}

	dbg->debug(__FUNCTION__, "zip archive opened");

	zip_uint64_t nentry = (zip_uint64_t) zip_get_num_entries(zip_archive, 0);
	for (zip_uint64_t idx = 0; idx < nentry; idx++) {
		struct zip_stat st;
		if (zip_stat_index(zip_archive, idx, 0, &st) == -1) {
			dbg->debug(__FUNCTION__, "cannot read file stat %d in zip archive: %s", idx, zip_strerror(zip_archive));
			continue;
		}

		zip_file_t *zip_file;
		if ((zip_file = zip_fopen_index(zip_archive, idx, 0)) == NULL) {
			dbg->debug(__FUNCTION__, "cannot open file %s (index %d) in zip_archive: %s", st.name, idx, zip_strerror(zip_archive));
			continue;
		}

		char *contents = new char[st.size];
		zip_int64_t read_size;
		if ((read_size = zip_fread(zip_file, contents, st.size)) == -1) {
			dbg->debug(__FUNCTION__, "failed to read content in file %s (index %d) of zip_archive: %s", st.name, idx, zip_file_strerror(zip_file));
		}
		else {
			if (read_size != (zip_int64_t) st.size) {
				dbg->debug(__FUNCTION__, "unexpected content size in file %s (index %d) of zip_archive; is %d, should be %d: %s", st.name, idx, read_size, st.size);
			}
			else {
				// path may start with simutrans/, in which case it can be removed safely
				bool start_with_simutrans = strncmp(st.name, "simutrans/", 10) == 0;
				const char* target_filename = start_with_simutrans ? st.name + 10 : st.name;

				char extracted_path[FILENAME_MAX];
				sprintf(extracted_path, "%s%s", env_t::data_dir, target_filename);
				dbg->debug(__FUNCTION__, "- %s: %d", extracted_path, st.size);

				if (!create_folder_if_required(extracted_path)) {
					continue;
				}

				if(st.size > 0 && !std::ofstream(extracted_path).write(contents, st.size)) {
					dbg->debug(__FUNCTION__, "Error writing file");
				}
			}
		}
		zip_fclose(zip_file);
	}

	if (zip_close(zip_archive) == -1) {
		dbg->debug(__FUNCTION__, "cannot close zip archive: %s", zip_strerror(zip_archive));
	}
}

static size_t curl_write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

static CURLcode curl_download_file(CURL *curl, const char* target_file, const char* url) {
	FILE *fp = fopen(target_file,"wb");
	CURLcode res;
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	char cabundle_path[FILENAME_MAX];
	sprintf(cabundle_path, "%s%s", env_t::data_dir, "cacert.pem");
	FILE *cabundle_file;
	if ((cabundle_file = fopen(cabundle_path, "r"))) {
		fclose(cabundle_file);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_CAINFO, cabundle_path);
	} else {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		dbg->warning(__FUNCTION__, "ssl certificate authority bundle not found at %s; https calls will not be validated; this may be a security concern", cabundle_path);
	}
	curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
	res = curl_easy_perform(curl);
	fclose(fp);
	return res;
}

// native download (curl), extract (libzip)
bool pakinstaller_t::action_triggered(gui_action_creator_t*, value_t)
{
	CURL *curl = curl_easy_init(); // can only be called once during program lifecycle
	if (curl) {
		dr_chdir( env_t::data_dir );
		dbg->debug(__FUNCTION__, "libcurl initialized");

		char outfilename[FILENAME_MAX];
		FOR(vector_tpl<sint32>, i, paks.get_selections()) {
			sprintf(outfilename, "%s%s.zip", env_t::data_dir, pakinfo[i*2 + 1]);

			CURLcode res = curl_download_file(curl, outfilename, pakinfo[i*2]);
			dbg->debug(__FUNCTION__, "pak target %s", pakinfo[i*2]);

			if (res == 0) {
				dbg->debug(__FUNCTION__, "download successful to %s, attempting extract", outfilename);
				read_zip(outfilename);
			} else {
				dbg->debug(__FUNCTION__, "download failed with error code %s, check curl errors; skipping", curl_easy_strerror(res));
			}
		}
		curl_easy_cleanup(curl);
	}
	else {
		dbg->debug(__FUNCTION__, "libcurl failed to initialize, pakset not downloaded");
	}

	finish_install = true;
	return false;
}
#endif
