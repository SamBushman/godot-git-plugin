#include "git_api.h"

namespace godot {

GitAPI *GitAPI::singleton = NULL;

void GitAPI::_register_methods() {
	register_method("_process", &GitAPI::_process);

	register_method("_commit", &GitAPI::_commit);
	register_method("_is_vcs_initialized", &GitAPI::_is_vcs_initialized);
	register_method("_get_modified_files_data", &GitAPI::_get_modified_files_data);
	register_method("_get_file_diff", &GitAPI::_get_file_diff);
	register_method("_get_project_name", &GitAPI::_get_project_name);
	register_method("_get_vcs_name", &GitAPI::_get_vcs_name);
	register_method("_initialize", &GitAPI::_initialize);
	register_method("_shut_down", &GitAPI::_shut_down);
	register_method("_stage_file", &GitAPI::_stage_file);
	register_method("_unstage_file", &GitAPI::_unstage_file);

	register_method("_set_credentials", &GitAPI::_set_credentials);
	register_method("_get_remotes", &GitAPI::_get_remotes);
	register_method("_create_remote", &GitAPI::_create_remote);
	register_method("_remove_remote", &GitAPI::_remove_remote);
	register_method("_fetch", &GitAPI::_fetch);
	register_method("_pull", &GitAPI::_pull);
	register_method("_push", &GitAPI::_push);
}

void GitAPI::_commit(const String p_msg) {
	if (!can_commit) {
		godot::Godot::print("Git API cannot commit. Check previous errors.");
		return;
	}

	git_signature *default_sign;
	git_oid tree_id, parent_commit_id, new_commit_id;
	git_tree *tree;
	git_index *repo_index;
	git_commit *parent_commit;

	GIT2_CALL(git_repository_index(&repo_index, repo), "Could not get repository index", NULL);
	for (int i = 0; i < staged_files.size(); i++) {
		String file_path = staged_files[i];
		File *file = File::_new();
		if (file->file_exists(file_path)) {
			GIT2_CALL(git_index_add_bypath(repo_index, file_path.alloc_c_string()), "Could not add file by path", NULL);
		} else {
			GIT2_CALL(git_index_remove_bypath(repo_index, file_path.alloc_c_string()), "Could not add file by path", NULL);
		}
	}
	GIT2_CALL(git_index_write_tree(&tree_id, repo_index), "Could not write index to tree", NULL);
	GIT2_CALL(git_index_write(repo_index), "Could not write index to disk", NULL);
	GIT2_CALL(git_signature_default(&default_sign, repo), "Could not get default signature", NULL);
	GIT2_CALL(git_tree_lookup(&tree, repo, &tree_id), "Could not lookup tree from ID", NULL);

	GIT2_CALL(git_reference_name_to_id(&parent_commit_id, repo, "HEAD"), "Could not get parent ID", NULL);
	GIT2_CALL(git_commit_lookup(&parent_commit, repo, &parent_commit_id), "Could not lookup parent commit data", NULL);

	GIT2_CALL(
			git_commit_create_v(
					&new_commit_id,
					repo,
					"HEAD",
					default_sign,
					default_sign,
					"UTF-8",
					p_msg.alloc_c_string(),
					tree,
					1,
					parent_commit),
			"Could not create commit",
			NULL);

	staged_files.clear();

	git_index_free(repo_index);
	git_signature_free(default_sign);
	git_commit_free(parent_commit);
	git_tree_free(tree);
}

void GitAPI::_stage_file(const String p_file_path) {
	if (staged_files.find(p_file_path) == -1) {
		staged_files.push_back(p_file_path);
	}
}

void GitAPI::_unstage_file(const String p_file_path) {
	if (staged_files.find(p_file_path) != -1) {
		staged_files.erase(p_file_path);
	}
}

void GitAPI::create_gitignore_and_gitattributes() {
	File *file = File::_new();

	if (!file->file_exists("res://.gitignore")) {
		file->open("res://.gitignore", File::ModeFlags::WRITE);
		file->store_string(
				"# Import cache\n"
				".import/\n\n"

				"# Binaries\n"
				"bin/\n"
				"build/\n"
				"lib/\n");
		file->close();
	}

	if (!file->file_exists("res://.gitattributes")) {
		file->open("res://.gitattributes", File::ModeFlags::WRITE);
		file->store_string(
				"# Set the default behavior, in case people don't have core.autocrlf set.\n"
				"* text=auto\n\n"

				"# Explicitly declare text files you want to always be normalized and converted\n"
				"# to native line endings on checkout.\n"
				"*.cpp text\n"
				"*.c text\n"
				"*.h text\n"
				"*.gd text\n"
				"*.cs text\n\n"

				"# Declare files that will always have CRLF line endings on checkout.\n"
				"*.sln text eol=crlf\n\n"

				"# Denote all files that are truly binary and should not be modified.\n"
				"*.png binary\n"
				"*.jpg binary\n");
		file->close();
	}
}

bool GitAPI::create_initial_commit() {
	git_signature *sig;
	git_oid tree_id, commit_id;
	git_index *repo_index;
	git_tree *tree;

	if (git_signature_default(&sig, repo) != 0) {
		godot::Godot::print_error("Unable to create a commit signature. Perhaps 'user.name' and 'user.email' are not set. Set default user name and user email by `git config` and initialize again", __func__, __FILE__, __LINE__);
		return false;
	}
	GIT2_CALL(git_repository_index(&repo_index, repo), "Could not get repository index", NULL);
	GIT2_CALL(git_index_write_tree(&tree_id, repo_index), "Could not create intial commit", NULL);

	GIT2_CALL(git_tree_lookup(&tree, repo, &tree_id), "Could not create intial commit", NULL);
	GIT2_CALL(
			git_commit_create_v(
					&commit_id,
					repo,
					"HEAD",
					sig,
					sig,
					NULL,
					"Initial commit",
					tree,
					0),
			"Could not create the initial commit",
			NULL);

	GIT2_CALL(git_index_write(repo_index), "Could not write index to disk", NULL);
	git_index_free(repo_index);
	git_tree_free(tree);
	git_signature_free(sig);

	return true;
}

bool GitAPI::_is_vcs_initialized() {
	return is_initialized;
}

Dictionary GitAPI::_get_modified_files_data() {
	git_status_options opts = GIT_STATUS_OPTIONS_INIT;
	opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
	opts.flags = GIT_STATUS_OPT_EXCLUDE_SUBMODULES;
	opts.flags |= GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX | GIT_STATUS_OPT_SORT_CASE_SENSITIVELY | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

	git_status_list *statuses = NULL;
	GIT2_CALL(git_status_list_new(&statuses, repo, &opts), "Could not get status information from repository", NULL);

	Dictionary diff; // Schema is <file_path, status>
	size_t count = git_status_list_entrycount(statuses);
	for (size_t i = 0; i < count; ++i) {
		const git_status_entry *entry = git_status_byindex(statuses, i);
		String path;
		if (entry->index_to_workdir) {
			path = entry->index_to_workdir->new_file.path;
		} else {
			path = entry->head_to_index->new_file.path;
		}
		switch (entry->status) {
			case GIT_STATUS_INDEX_NEW:
			case GIT_STATUS_WT_NEW: {
				diff[path] = 0;
			} break;
			case GIT_STATUS_INDEX_MODIFIED:
			case GIT_STATUS_WT_MODIFIED: {
				diff[path] = 1;
			} break;
			case GIT_STATUS_INDEX_RENAMED:
			case GIT_STATUS_WT_RENAMED: {
				diff[path] = 2;
			} break;
			case GIT_STATUS_INDEX_DELETED:
			case GIT_STATUS_WT_DELETED: {
				diff[path] = 3;
			} break;
			case GIT_STATUS_INDEX_TYPECHANGE:
			case GIT_STATUS_WT_TYPECHANGE: {
				diff[path] = 4;
			} break;
		}
	}

	git_status_list_free(statuses);

	return diff;
}

Array GitAPI::_get_file_diff(const String file_path) {
	git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
	git_diff *diff;
	char *pathspec = file_path.alloc_c_string();

	opts.context_lines = 3;
	opts.interhunk_lines = 0;
	opts.flags = GIT_DIFF_DISABLE_PATHSPEC_MATCH | GIT_DIFF_INCLUDE_UNTRACKED;
	opts.pathspec.strings = &pathspec;
	opts.pathspec.count = 1;

	GIT2_CALL(git_diff_index_to_workdir(&diff, repo, NULL, &opts), "Could not create diff for index from working directory", NULL);

	diff_contents.clear();
	GIT2_CALL(git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, diff_line_callback_function, NULL), "Call to diff handler provided unsuccessful", NULL);

	git_diff_free(diff);

	return diff_contents;
}

String GitAPI::_get_project_name() {
	return String("project");
}

String GitAPI::_get_vcs_name() {
	return "Git";
}

bool GitAPI::_initialize(const String p_project_root_path) {
	ERR_FAIL_COND_V(p_project_root_path == "", false);

	singleton = this;

	int init = git_libgit2_init();
	if (init > 1) {
		WARN_PRINT("Multiple libgit2 instances are running");
	}

	if (repo) {
		return true;
	}

	can_commit = true;
	GIT2_CALL(git_repository_init(&repo, p_project_root_path.alloc_c_string(), 0), "Could not initialize repository", NULL);
	if (git_repository_head_unborn(repo) == 1) {
		create_gitignore_and_gitattributes();
		if (!create_initial_commit()) {
			godot::Godot::print_error("Initial commit could not be created. Commit functionality will not work.", __func__, __FILE__, __LINE__);
			can_commit = false;
		}
	}

	GIT2_CALL(git_repository_open(&repo, p_project_root_path.alloc_c_string()), "Could not open repository", NULL);
	is_initialized = true;

	return is_initialized;
}

bool GitAPI::_shut_down() {
	git_repository_free(repo);

	GIT2_CALL(git_libgit2_shutdown(), "Could not shutdown Git Addon", NULL);

	return true;
}

String GitAPI::_get_current_branch_name() {
	git_reference *head = nullptr;
	if (git_repository_head(&head, repo) != 0) {
		return String();
	}
	String name = git_reference_shorthand(head);
	git_reference_free(head);
	return name;
}

void GitAPI::_set_credentials(const String username, const String password, const String ssh_public_key_path, const String ssh_private_key_path, const String ssh_passphrase) {
	creds.username = username;
	creds.password = password;
	creds.ssh_public_key_path = ssh_public_key_path;
	creds.ssh_private_key_path = ssh_private_key_path;
	creds.ssh_passphrase = ssh_passphrase;
}

Array GitAPI::_get_remotes() {
	git_strarray remote_names = { NULL, 0 };
	GIT2_CALL(git_remote_list(&remote_names, repo), "Could not get list of remotes", NULL);

	Array remotes;
	for (size_t i = 0; i < remote_names.count; i++) {
		remotes.push_back(String(remote_names.strings[i]));
	}
	git_strarray_free(&remote_names);

	return remotes;
}

void GitAPI::_create_remote(const String remote_name, const String remote_url) {
	git_remote *remote = nullptr;
	GIT2_CALL(git_remote_create(&remote, repo, remote_name.alloc_c_string(), remote_url.alloc_c_string()), "Could not create remote", NULL);
	if (remote) {
		git_remote_free(remote);
	}
}

void GitAPI::_remove_remote(const String remote_name) {
	GIT2_CALL(git_remote_delete(repo, remote_name.alloc_c_string()), "Could not delete remote", remote_name.alloc_c_string());
}

static void _fill_remote_callbacks(git_remote_callbacks &remote_cbs, Credentials *creds) {
	remote_cbs = GIT_REMOTE_CALLBACKS_INIT;
	remote_cbs.credentials = &credentials_cb;
	remote_cbs.update_tips = &update_cb;
	remote_cbs.sideband_progress = &progress_cb;
	remote_cbs.transfer_progress = &transfer_progress_cb;
	remote_cbs.payload = creds;
	remote_cbs.push_transfer_progress = &push_transfer_progress_cb;
	remote_cbs.push_update_reference = &push_update_reference_cb;
}

void GitAPI::_fetch(const String remote) {
	Godot::print("GitAPI: Performing fetch from " + remote);

	git_remote *remote_object = nullptr;
	if (git_remote_lookup(&remote_object, repo, remote.alloc_c_string()) != 0) {
		check_git2_errors(-1, "Could not lookup remote", remote.alloc_c_string());
		return;
	}

	git_remote_callbacks remote_cbs;
	_fill_remote_callbacks(remote_cbs, &creds);

	if (git_remote_connect(remote_object, GIT_DIRECTION_FETCH, &remote_cbs, nullptr, nullptr) != 0) {
		check_git2_errors(-1, "Could not connect to remote (check your credentials)", remote.alloc_c_string());
		git_remote_free(remote_object);
		return;
	}

	git_fetch_options opts = GIT_FETCH_OPTIONS_INIT;
	opts.callbacks = remote_cbs;
	GIT2_CALL(git_remote_fetch(remote_object, nullptr, &opts, "fetch"), "Could not fetch data from remote", NULL);

	git_remote_free(remote_object);

	Godot::print("GitAPI: Fetch ended");
}

void GitAPI::_pull(const String remote) {
	Godot::print("GitAPI: Performing pull from " + remote);

	git_remote *remote_object = nullptr;
	if (git_remote_lookup(&remote_object, repo, remote.alloc_c_string()) != 0) {
		check_git2_errors(-1, "Could not lookup remote", remote.alloc_c_string());
		return;
	}

	git_remote_callbacks remote_cbs;
	_fill_remote_callbacks(remote_cbs, &creds);

	if (git_remote_connect(remote_object, GIT_DIRECTION_FETCH, &remote_cbs, nullptr, nullptr) != 0) {
		check_git2_errors(-1, "Could not connect to remote (check your credentials)", remote.alloc_c_string());
		git_remote_free(remote_object);
		return;
	}

	git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
	fetch_opts.callbacks = remote_cbs;

	String branch_name = _get_current_branch_name();
	char *ref_spec_str = String("refs/heads/" + branch_name).alloc_c_string();
	char *ref[] = { ref_spec_str };
	git_strarray refspec = { ref, 1 };

	int fetch_err = git_remote_fetch(remote_object, &refspec, &fetch_opts, "pull");
	git_remote_free(remote_object);
	if (fetch_err != 0) {
		check_git2_errors(-1, "Could not fetch data from remote", NULL);
		return;
	}

	pull_merge_oid = {};
	GIT2_CALL(git_repository_fetchhead_foreach(repo, fetchhead_foreach_cb, &pull_merge_oid), "Could not read \"FETCH_HEAD\" file", NULL);

	if (git_oid_is_zero(&pull_merge_oid)) {
		Godot::print_error("GitAPI: Could not find remote branch HEAD for " + branch_name + ". Try pushing the branch first.", __func__, __FILE__, __LINE__);
		return;
	}

	git_annotated_commit *fetchhead_annotated_commit = nullptr;
	if (git_annotated_commit_lookup(&fetchhead_annotated_commit, repo, &pull_merge_oid) != 0) {
		check_git2_errors(-1, "Could not get merge commit", NULL);
		return;
	}

	const git_annotated_commit *merge_heads[] = { fetchhead_annotated_commit };

	git_merge_analysis_t merge_analysis;
	git_merge_preference_t preference = GIT_MERGE_PREFERENCE_NONE;
	int analysis_err = git_merge_analysis(&merge_analysis, &preference, repo, merge_heads, 1);
	if (analysis_err != 0) {
		check_git2_errors(-1, "Merge analysis failed", NULL);
		git_annotated_commit_free(fetchhead_annotated_commit);
		return;
	}

	if (merge_analysis & GIT_MERGE_ANALYSIS_FASTFORWARD) {
		git_checkout_options ff_checkout_options = GIT_CHECKOUT_OPTIONS_INIT;
		ff_checkout_options.checkout_strategy = GIT_CHECKOUT_SAFE;

		git_reference *target_ref = nullptr;
		GIT2_CALL(git_repository_head(&target_ref, repo), "Failed to get HEAD reference", NULL);

		git_object *target = nullptr;
		GIT2_CALL(git_object_lookup(&target, repo, &pull_merge_oid, GIT_OBJECT_COMMIT), "Failed to lookup fetched commit", NULL);

		if (target) {
			GIT2_CALL(git_checkout_tree(repo, target, &ff_checkout_options), "Failed to checkout HEAD reference", NULL);
			git_object_free(target);
		}

		if (target_ref) {
			git_reference *new_target_ref = nullptr;
			GIT2_CALL(git_reference_set_target(&new_target_ref, target_ref, &pull_merge_oid, nullptr), "Failed to move HEAD reference", NULL);
			if (new_target_ref) {
				git_reference_free(new_target_ref);
			}
			git_reference_free(target_ref);
		}

		Godot::print("GitAPI: Fast Forwarded");
		GIT2_CALL(git_repository_state_cleanup(repo), "Could not clean repository state", NULL);

	} else if (merge_analysis & GIT_MERGE_ANALYSIS_NORMAL) {
		git_merge_options merge_opts = GIT_MERGE_OPTIONS_INIT;
		git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;

		merge_opts.file_favor = GIT_MERGE_FILE_FAVOR_NORMAL;
		merge_opts.file_flags = (GIT_MERGE_FILE_STYLE_DIFF3 | GIT_MERGE_FILE_DIFF_MINIMAL);
		checkout_opts.checkout_strategy = (GIT_CHECKOUT_SAFE | GIT_CHECKOUT_ALLOW_CONFLICTS | GIT_CHECKOUT_CONFLICT_STYLE_MERGE);
		GIT2_CALL(git_merge(repo, merge_heads, 1, &merge_opts, &checkout_opts), "Merge Failed", NULL);

		git_index *index = nullptr;
		GIT2_CALL(git_repository_index(&index, repo), "Could not get repository index", NULL);

		if (index && git_index_has_conflicts(index)) {
			Godot::print_error("GitAPI: Index has conflicts. Solve conflicts and make a merge commit.", __func__, __FILE__, __LINE__);
		} else {
			Godot::print("GitAPI: Changes are staged, make a merge commit.");
		}
		if (index) {
			git_index_free(index);
		}

		has_merge = true;

	} else if (merge_analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) {
		Godot::print("GitAPI: Already up to date");
		GIT2_CALL(git_repository_state_cleanup(repo), "Could not clean repository state", NULL);

	} else {
		Godot::print("GitAPI: Can not merge");
	}

	git_annotated_commit_free(fetchhead_annotated_commit);

	Godot::print("GitAPI: Pull ended");
}

void GitAPI::_push(const String remote, const bool force) {
	Godot::print("GitAPI: Performing push to " + remote);

	git_remote *remote_object = nullptr;
	if (git_remote_lookup(&remote_object, repo, remote.alloc_c_string()) != 0) {
		check_git2_errors(-1, "Could not lookup remote", remote.alloc_c_string());
		return;
	}

	git_remote_callbacks remote_cbs;
	_fill_remote_callbacks(remote_cbs, &creds);

	if (git_remote_connect(remote_object, GIT_DIRECTION_PUSH, &remote_cbs, nullptr, nullptr) != 0) {
		check_git2_errors(-1, "Could not connect to remote (check your credentials)", remote.alloc_c_string());
		git_remote_free(remote_object);
		return;
	}

	String branch_name = _get_current_branch_name();
	char *pushspec_str = (String() + (force ? "+" : "") + "refs/heads/" + branch_name).alloc_c_string();
	git_strarray refspec = { &pushspec_str, 1 };

	git_push_options push_options = GIT_PUSH_OPTIONS_INIT;
	push_options.callbacks = remote_cbs;

	GIT2_CALL(git_remote_push(remote_object, &refspec, &push_options), "Failed to push", NULL);

	git_remote_free(remote_object);

	Godot::print("GitAPI: Push ended");
}

void GitAPI::_init() {
}

void GitAPI::_process() {
}

GitAPI::GitAPI() {
}

GitAPI::~GitAPI() {
}

} // namespace godot
