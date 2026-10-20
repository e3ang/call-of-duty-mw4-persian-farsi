"""Append a PE import dependency with an empty thunk table; existing imports stay in place."""
import pathlib,struct,pefile
def add_dependency(source,dest,name,symbol=None):
    data=bytearray(pathlib.Path(source).read_bytes());pe=pefile.PE(data=data)
    align=lambda x,a:(x+a-1)//a*a
    table_end=pe.sections[-1].get_file_offset()+40
    assert table_end+40<=min(s.PointerToRawData for s in pe.sections if s.PointerToRawData)
    sa=pe.OPTIONAL_HEADER.SectionAlignment;fa=pe.OPTIONAL_HEADER.FileAlignment
    rva=align(max(s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData) for s in pe.sections),sa)
    raw=align(len(data),fa)
    descriptors=[]
    directory=pe.OPTIONAL_HEADER.DATA_DIRECTORY[1]
    off=pe.get_offset_from_rva(directory.VirtualAddress)
    while any(data[off:off+20]):descriptors.append(bytes(data[off:off+20]));off+=20
    payload=bytearray(b''.join(descriptors)+bytes(40))
    name_offset=len(payload);payload+=name.encode('ascii')+b'\0'
    payload+=bytes((-len(payload))%8);thunk_offset=len(payload);payload+=bytes(16)
    if symbol:
        payload+=bytes(16)
        symbol_offset=len(payload);payload+=b'\0\0'+symbol.encode('ascii')+b'\0'
        struct.pack_into('<Q',payload,thunk_offset,rva+symbol_offset)
        struct.pack_into('<Q',payload,thunk_offset+16,rva+symbol_offset)
    struct.pack_into('<IIIII',payload,len(descriptors)*20,rva+thunk_offset,0,0,rva+name_offset,rva+thunk_offset+(16 if symbol else 8))
    size=len(payload);raw_size=align(size,fa)
    header=struct.pack('<8sIIIIIIHHI',b'.fasplash',size,rva,raw_size,raw,0,0,0,0,0xc0000040)
    data[table_end:table_end+40]=header
    struct.pack_into('<H',data,pe.FILE_HEADER.get_field_absolute_offset('NumberOfSections'),len(pe.sections)+1)
    struct.pack_into('<I',data,pe.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfImage'),align(rva+size,sa))
    struct.pack_into('<II',data,directory.get_file_offset(),rva,(len(descriptors)+2)*20)
    struct.pack_into('<I',data,pe.OPTIONAL_HEADER.get_field_absolute_offset('CheckSum'),0)
    data+=bytes(raw-len(data));data+=payload+bytes(raw_size-size)
    pathlib.Path(dest).write_bytes(data)
    verified=pefile.PE(data=data)
    for section in pe.sections:
        assert verified.get_data(section.VirtualAddress,section.SizeOfRawData)==pe.get_data(section.VirtualAddress,section.SizeOfRawData)
    return {'dependency':name,'new_section_rva':hex(rva),'original_sections_unchanged':True}
if __name__=='__main__':
    import sys,json
    print(json.dumps(add_dependency(*sys.argv[1:])))
