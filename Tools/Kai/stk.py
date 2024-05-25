import sys
import os

def show_stk(lines):
    for l in lines:
        if l.find('[bt]') >= 0:
            ea = l.split(' ')[2]
            if ea.find('(+') > 0:
                eas = ea[:-1].split('(+')
                os.system('addr2line -e {} -f {} > .stk'.format(eas[0], eas[1]))
                with open('.stk', 'r') as sfd:
                    stks = sfd.readlines()
                    for sk in stks:
                        if sk.find('_Z') == 0:
                            os.system('c++filt {}'.format(sk))
                            break
            else:
                print(l)

try:
    fd = open(sys.argv[1], 'r')
    lines = fd.readlines()
    fd.close()
    show_stk(lines)
    sys.exit(0)
except IOError:
    print('Not a file')

lines = sys.argv[1].split('\n')
if len(lines) != 0:
    show_stk(lines)
