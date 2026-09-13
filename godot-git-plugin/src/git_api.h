#ifndef GIT_API_H
#define GIT_API_H

#include <Button.hpp>
#include <Control.hpp>
#include <Directory.hpp>
#include <EditorVCSInterface.hpp>
#include <File.hpp>
#include <Godot.hpp>
#include <PanelContainer.hpp>

#include <allocation_defs.h>
#include <git_common.h>

#include <git2.h>

namespace godot {

class GitAPI : public EditorVCSInterface {
	GODOT_CLASS(GitAPI, EditorVCSInterface)

	static GitAPI *singleton;

	bool is_initialized;
	bool can_commit;

	Array staged_files;

	PanelContainer *init_settings_panel_container;
	Button *init_settings_button;

	git_repository *repo = nullptr;

	Credentials creds;
	bool has_merge = false;
	git_oid pull_merge_oid = {};

	void _commit(const String p_msg);
	bool _is_vcs_initialized();
	Array _get_modified_files_data();
	Array _get_file_diff(const String file_path);
	String _get_project_name();
	String _get_vcs_name();
	bool _initialize(const String p_project_root_path);
	bool _shut_down();
	void _stage_file(const String p_file_path);
	void _unstage_file(const String p_file_path);

	// Remote operations.
	void _set_credentials(const String username, const String password, const String ssh_public_key_path, const String ssh_private_key_path, const String ssh_passphrase);
	Array _get_remotes();
	void _create_remote(const String remote_name, const String remote_url);
	void _remove_remote(const String remote_name);
	void _fetch(const String remote);
	void _pull(const String remote);
	void _push(const String remote, const bool force);

	String _get_current_branch_name();

	// Diff / discard / branch management.
	Array _get_diff(const String identifier, const int64_t area);
	Array _parse_diff(git_diff *p_diff);
	void _discard_file(const String file_path);
	Array _get_branch_list();
	void _create_branch(const String branch_name);
	void _remove_branch(const String branch_name);
	bool _checkout_branch(const String branch_name);

public:
	static void _register_methods();

	static GitAPI *get_singleton() { return singleton; }

	Array diff_contents;

	void create_gitignore_and_gitattributes();
	bool create_initial_commit();

	void _init();
	void _process();

	GitAPI();
	~GitAPI();
};

} // namespace godot

#endif // !GIT_API_H
