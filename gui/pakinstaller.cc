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

// native download (curl), extract and install
bool pakinstaller_t::action_triggered(gui_action_creator_t*, value_t)
{
	CURL *curl = curl_easy_init(); // can only be called once during program lifecycle
	if (curl) {
		dr_chdir( env_t::data_dir );
		dbg->debug(__FUNCTION__, "libcurl initialized");
		FOR(vector_tpl<sint32>, i, paks.get_selections()) {
			CURLcode res;
			curl_easy_setopt(curl, CURLOPT_URL, pakinfo[i*2]);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);
			res = curl_easy_perform(curl);
			dbg->debug(__FUNCTION__, "pak target %s", pakinfo[i*2]);
			if (res != 0) {
				dbg->debug(__FUNCTION__, "download failed with error code %d, check curl errors; skipping", res);
				continue;
			}
			dbg->debug(__FUNCTION__, "download successful, attempting extract");

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
