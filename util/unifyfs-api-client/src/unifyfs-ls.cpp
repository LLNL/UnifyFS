// This is a very simple program that is designed to test the new
// get_gfids rpc I've added to UnifyFS.
//

#include "unifyfs_api.h"

#include <getopt.h>

#include <cstring>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>


// values that can be passed in via command line parameters
// (filled in by the ParseOpts() function
struct CommandOptions
{
    std::string mount_point;  // where is UnifyFS mounted
    bool verbose;  // verbose display or not

    CommandOptions() : mount_point("/unifyfs"), verbose(false) {}
};

// Functions for parsing the arguments and printing some help text
// (Definitions below)
void parseOpts( int argc, char **argv, CommandOptions &cmdLineOpts);
void printHelp( const char *exeName);


// Comparison class for the std::set we use
struct CompareByFilename {
    bool operator()(const unifyfs_server_file_meta& a, const unifyfs_server_file_meta& b) const
    {
        int comp = strcmp(a.filename, b.filename);
        if (comp >= 0) return false;
        return true;
    }
};

// Functions for printing out a file's metadata.
// We've got a choice of 2: print_fmeta() is more verbose.  print_fmeta_ls()
// looks similar to the output of the standard `ls` command.
// (Functions are defined down below.)
void print_fmeta( const unifyfs_server_file_meta& attr);
void print_fmeta_ls(const unifyfs_server_file_meta& attr);

int main( int argc, char **argv)
{

    CommandOptions opts;
    parseOpts( argc, argv, opts);

    unifyfs_handle fshdl;
    unifyfs_rc urc = unifyfs_initialize(opts.mount_point.c_str(),
                                        NULL, 0, &fshdl);
    if (UNIFYFS_SUCCESS != urc) {
        std::cerr << "UNIFYFS ERROR: init failed at mountpoint "
                  << opts.mount_point.c_str() << " - "
                  << unifyfs_rc_enum_description(urc) << std::endl;
        return 1;
    }

    std::cout << "Attempting to call unifyfs_get_gfids()..." << std::endl;
    int num_gfids = -1;
    unifyfs_gfid *gfid_list;
    int ret = unifyfs_get_gfid_list(fshdl, &num_gfids, &gfid_list);
    std::cout << "...and done.  ret=" << ret << "\tnum_gfids=" << num_gfids << std::endl;

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
                std::cerr << "\t!!!ERROR " << ret << " retrieving metadata for GFID "
                          << gfid_list[i] << "!!!" << std::endl;
            }
        }
    }


    // Now iterate through the set and print everything out (sorted by filename)
    std::cout << "GFIDs received:" << std::endl;
    auto it = file_set.cbegin();
    while (it != file_set.cend()) {
        print_fmeta_ls(*it++);
    }

    // Walk the set freeing the filename strings so we don't have any
    // memory leaks.
    // Note: set iterators are pretty much always const (regardless of whether
    // they come from begin() or cbegin()) because changes to set members
    // could affect the ordering within the set.  It looks like the standard
    // way to modify a set member is to remove it, make the changes and then
    // re-insert.
    // TL;DR: we can't explicitly set the pointer to NULL after calling free()
    // This isn't a problem since we're going to erase the whole set anyway.
    it = file_set.cbegin();
    while (it != file_set.end()) {
        free(it->filename);
        it++;
    }
    // explicitly erase all the elements so we don't accidentally reference
    // those now dangling filename pointers.
    file_set.erase(file_set.begin(), file_set.end());

    urc = unifyfs_finalize(fshdl);
    if (UNIFYFS_SUCCESS != urc) {
        std::cerr << "UNIFYFS ERROR: failed to finalize: "
                  << unifyfs_rc_enum_description(urc) << std::endl;
        return 3;
    }

    return 0;
}


#define tabout std::cout << "\t"
void print_fmeta( const unifyfs_server_file_meta& attr)
{
    std::cout << attr.filename << std::endl;
    tabout << "gfid:         " << attr.gfid << std::endl;
    tabout << "is_laminated: " << attr.is_laminated << std::endl;
    tabout << "is_shared:    " << attr.is_shared << std::endl;
    tabout << "mode:         0" << std::oct << attr.mode << std::dec << std::endl;
    tabout << "uid:          " << attr.uid << std::endl;
    tabout << "gid:          " << attr.gid << std::endl;
    tabout << "size:         " << attr.size << std::endl;
    // TODO: struct timespec has nanoseconds as well
    tabout << "atime:        " << attr.atime.tv_sec << std::endl;
    tabout << "mtime:        " << attr.mtime.tv_sec << std::endl;
    tabout << "ctime:        " << attr.ctime.tv_sec << std::endl;
}

// An output resembling what you'd get from 'ls -l'
void print_fmeta_ls(const unifyfs_server_file_meta& attr)
{
    std::cout << std::oct << std::right << std::setw(7) << attr.mode
              << std::dec << std::left << std::setw(0)
              << " " << attr.uid << " " << attr.gid
              << std::right << std::setw(8) << attr.size
              << std::left << std::setw(0)
              << " " << attr.mtime.tv_sec << " " << attr.filename << std::endl;
    // TODO: mode should be symbolic, not octal
    // TODO: uid & gid should be names if possible
    // TODO: mtime should be text not epoch seconds
}



struct option long_options[] = {
    {"mount_point", required_argument, 0, 'm'},
    {"verbose",     no_argument,       0, 'v'},
    {"help",        no_argument,       0, 'h'},
    {0, 0, 0, 0} };

void parseOpts( int argc, char **argv, CommandOptions &cmdLineOpts)
{
    while (1)
    {
        /* getopt_long stores the option index here. */
        int option_index = 0;
        int c = getopt_long( argc, argv, "m:vh?", long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
                break;

        switch (c)
        {
                case 'm':
                    cmdLineOpts.mount_point.assign(optarg);
                    break;

                case 'v':
                    cmdLineOpts.verbose = true;
                    break;

                case 'h':
                case '?':
                default:
                    printHelp( argv[0]);
                    exit( 0);
        }
    }
}

void printHelp( const char *exeName)
{
    using namespace std;
    CommandOptions opts;

    cout << "Usage:" << endl;
    cout << "  " << exeName << " [ -v | --verbose ] "
         << "[ -m <dir_name> | --mount_point_dir=<dir_name> ]" << endl;
    cout << endl;
    cout << "  " << "-v | --verbose: " << "show verbose information"
         << "(default: " << opts.verbose << ")" << endl;
    cout << "  " << "-m | --mount_point: " << "the location where unifyfs is mounted "
         << "(default: " << opts.mount_point << ")" << endl;
}
