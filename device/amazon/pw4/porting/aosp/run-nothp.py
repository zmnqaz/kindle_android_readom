# Kernel 7.0 ignores mmap address hints larger than 2MB while THP is on; Android 8.1 host ART
# relies on such hints to map boot.art. PR_SET_THP_DISABLE (41) is inherited by every child.
import ctypes, os, sys
if ctypes.CDLL(None).prctl(41, 1, 0, 0, 0) != 0:
    sys.exit('prctl(PR_SET_THP_DISABLE) failed')
os.execvp(sys.argv[1], sys.argv[1:])
