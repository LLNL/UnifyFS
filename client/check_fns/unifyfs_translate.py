from __future__ import print_function
import sys
import re

''' a modified version of Elsa's translate.py script here:
    https://github.com/LLNL/GOTCHA-tracer/blob/master/modules/translate.py
    this one just writes the gotcha struct to a header file
    and writes out the UNIFYFS_DEF macros

    to generate gotcha header run the following: python unifyfs_translate.py unifyfs_list
'''
class func:

	def __init__(self, prototype):
		l = prototype.strip().split('(')
                x = filter(None, l[0].split(' '))
                p = l[2:]
                self.params = [elem.strip("),") for elem in p]

		# set up useful vars
                self.ret_type = ' '.join(x[0:-1])
		self.wrap_macro = x[-1]
                self.real_macro = "UNIFYFS_REAL"
                self.macro_def  = "UNIFYFS_DEF"
		self.name = l[1].strip(")")

		self.name_quote = "\"" + self.name + "\""
		self.wrapper    = self.wrap_macro + "(" + self.name + ")"
		self.wrapee     = self.real_macro + "(" + self.name + ")"

	def __str__(self):
		return self.wrap_macro + ' (' + self.name + ')'

def read_functions_file(filename):
	functions = []
	f = open(filename, 'r')
	for line in f:
		functions.append(func(line))
	f.close()
	return functions

def write_gotcha_file(filename, func_list, modulename):
	m = modulename.split('_')[0]
	f = open(filename, 'w')

        # write each function definition in the list
        for function in func_list:
                f.write(function.macro_def + "(" + function.name + ", " + function.ret_type +
                        "," " (" + function.params[0] + "));" + "\n")

	# write map struct
	f.write("struct gotcha_binding_t wrap_" + modulename + "[] = {\n")
	for function in func_list:
		f.write("\t{ " + function.name_quote + ", " + function.wrapper + ", &" + function.wrapee + " },\n")
	f.write("};\n")

	# define number of functions we are wrapping for gotcha struct
	f.write("\n#define GOTCHA_NFUNCS (sizeof(wrap_unifyfs_list) / sizeof(wrap_unifyfs_list[0]))\n")

	# close struct and file
	f.close()

def main(modulename):
	functions = read_functions_file(modulename+".txt")
	write_gotcha_file("gotcha_map_"+modulename+".h", functions, modulename)

if __name__ == "__main__":
	if len(sys.argv) < 2:
		print("Usage: python", sys.argv[0], "modulename")
	else:
		main(sys.argv[1])
