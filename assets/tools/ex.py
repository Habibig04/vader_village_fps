import ctypes,sys,os
L=ctypes.CDLL("libarchive.so.13")
L.archive_read_new.restype=ctypes.c_void_p
for f in ["archive_read_support_filter_all","archive_read_support_format_all"]:
    getattr(L,f).argtypes=[ctypes.c_void_p]
L.archive_read_open_filename.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_size_t]
L.archive_read_next_header.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_void_p)]
L.archive_entry_pathname.argtypes=[ctypes.c_void_p];L.archive_entry_pathname.restype=ctypes.c_char_p
L.archive_read_data.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t];L.archive_read_data.restype=ctypes.c_ssize_t
L.archive_error_string.argtypes=[ctypes.c_void_p];L.archive_error_string.restype=ctypes.c_char_p
src,dst=sys.argv[1],sys.argv[2]
a=L.archive_read_new();L.archive_read_support_filter_all(a);L.archive_read_support_format_all(a)
r=L.archive_read_open_filename(a,src.encode(),65536)
if r:print("open err",L.archive_error_string(a));sys.exit(1)
e=ctypes.c_void_p();buf=ctypes.create_string_buffer(1<<20)
while L.archive_read_next_header(a,ctypes.byref(e))==0:
    p=L.archive_entry_pathname(e).decode('utf8','replace');out=os.path.join(dst,p)
    if p.endswith('/'):os.makedirs(out,exist_ok=True);continue
    if os.path.isdir(out):continue
    os.makedirs(os.path.dirname(out),exist_ok=True)
    with open(out,'wb') as fo:
        while True:
            n=L.archive_read_data(a,buf,len(buf))
            if n<=0:break
            fo.write(buf.raw[:n])
