// This is a very simple program that is designed to test the new
// get_gfids rpc I've added to UnifyFS.
// 

#include "unifyfs_api.h"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <set>
using namespace std;


#ifdef __cplusplus
extern "C" {
#endif
// Definition of the UnifyFS functions we need to call
// (The functions aren't really intended to be called from outside the
// library, but they do exist.)
//int unifyfs_get_gfids(int *num_gfids, int **gfid_list);
//int unifyfs_client_metaget( int gfid, unifyfs_file_attr_t* gfattr);

#ifdef __cplusplus
} // extern "C"
#endif


// Comparison class for the std::set we use
struct CompareByFilename {
    bool operator()(const unifyfs_server_file_meta& a, const unifyfs_server_file_meta& b) const
    {
        int comp = strcmp(a.filename, b.filename);
        if (comp >= 0) return false;
        return true;
    }
};


// Quick functions to print out a file's metadata.  Defined down below.
// TODO:We've got a choice of 2.  I haven't decided which one I like better yet.
void print_fmeta( const unifyfs_server_file_meta& attr);
void print_fmeta_ls(const unifyfs_server_file_meta& attr);


int main( int argc, char **argv)
{
    // TODO: get this from command line options!
    const char mountpt[]="/unifyfs";

    unifyfs_handle fshdl;
    unifyfs_rc urc = unifyfs_initialize(mountpt, NULL, 0, &fshdl);
    if (UNIFYFS_SUCCESS != urc) {
        fprintf(stderr, "UNIFYFS ERROR: init failed at mountpoint %s - %s\n",
                mountpt, unifyfs_rc_enum_description(urc));
        return 1;
    }

    cout << "Attempting to call unifyfs_get_gfids()..." << endl;
    int num_gfids = -1;
    unifyfs_gfid *gfid_list;
    int ret = unifyfs_get_gfid_list(fshdl, &num_gfids, &gfid_list);
    cout << "...and done.  ret=" << ret << "\tnum_gfids=" << num_gfids << endl;

    std::set<unifyfs_server_file_meta, CompareByFilename> file_set;
    if (UNIFYFS_SUCCESS==ret && num_gfids > 0) {
        unifyfs_server_file_meta fmeta;
        
        for (unsigned i = 0; i < num_gfids; i++) {
            //ret = unifyfs_stat(fshdl, gfid_list[i], &gfattr);
            ret = unifyfs_get_server_file_meta(fshdl, gfid_list[i], &fmeta);
            if (UNIFYFS_SUCCESS == ret) {
                file_set.insert(fmeta);
                // Note: insert() does a shallow copy and does *not* allocate
                // new memory for the fmeta.filename pointer.  That's fine
                // since fmeta is going to get overwritten in the next loop
                // iteration anyway.  We do need to rember to explicitly free
                // the memory when removing items from the set, though.
            } else {
                cout << "\t!!!ERROR " << ret << " retrieving metadata for GFID " << gfid_list[i] << "!!!" << endl;
            }
        }
    }

    
    // Now iterate through the set and print everything out (sorted by filename)
    cout << "GFIDs received:" << endl;
    auto it = file_set.cbegin();
    while (it != file_set.cend()) {
        print_fmeta_ls(*it++);
    }

    // Walk the set freeing the filename strings so we don't have any
    // memory leaks.
    // Note: set iterators are pretty much always const becuase changes to set
    // members could affect the ordering within the set.  It looks like the
    // standard way to modify a set member is to remove it, make the changes
    // and then re-insert.
    // TL;DR: we can't explicitly set the pointer to NULL after calling free()
    // This isn't a problem since we're going to erase the whole set anyway.
    it = file_set.begin();  // For sets, begin() or cbegin() both return a const_iterator  
    while (it != file_set.end()) {
        free(it->filename);
        it++;
    }
    // explicitly erase all the elements so we don't actually reference 
    // those now dangling filename pointers.
    file_set.erase(file_set.begin(), file_set.end());


    urc = unifyfs_finalize(fshdl);
    if (UNIFYFS_SUCCESS != urc) {
        fprintf(stderr, "UNIFYFS ERROR: failed to finalize - %s\n",
                unifyfs_rc_enum_description(urc));
        return 3;
    }

    return 0;
}


#define tabout cout << "\t"
void print_fmeta( const unifyfs_server_file_meta& attr)
{
    cout << attr.filename << endl;
    tabout << "gfid:         " << attr.gfid << endl;
    tabout << "is_laminated: " << attr.is_laminated << endl;
    tabout << "is_shared:    " << attr.is_shared << endl;
    tabout << "mode:         0" << std::oct << attr.mode << std::dec << endl;
    tabout << "uid:          " << attr.uid << endl;
    tabout << "gid:          " << attr.gid << endl;
    tabout << "size:         " << attr.size << endl;
    // TODO: struct timespec has nanoseconds as well
    tabout << "atime:        " << attr.atime.tv_sec << endl;
    tabout << "mtime:        " << attr.mtime.tv_sec << endl;
    tabout << "ctime:        " << attr.ctime.tv_sec << endl;
}

// An output resembling what you'd get from 'ls -l'
void print_fmeta_ls(const unifyfs_server_file_meta& attr)
{
    cout << std::oct << std::right << std::setw(7) << attr.mode << std::dec << std::left << std::setw(0) 
         << " " << attr.uid << " " << attr.gid
         << std::right << std::setw(8) << attr.size << std::left << std::setw(0)
         << " " << attr.mtime.tv_sec << " " << attr.filename << endl;
    // TODO: mode mode should be symbolic, not octal
    // TODO: uid & gid should be names if possible
    // TODO: mtime should be text not epoch seconds
}

