##     object.method_name            function
native $buffer.length()              buflen
native $buffer.replace()             buf_replace
native $buffer.subrange()            subbuf
native $buffer.bufsub()              bufsub
native $buffer.to_string()           buf_to_str
native $buffer.to_strings()          buf_to_strings
native $buffer.from_string()         str_to_buf
native $buffer.from_strings()        strings_to_buf
native $dictionary.values()          dict_values
native $dictionary.keys()            dict_keys
native $dictionary.add()             dict_add
native $dictionary.union()           dict_union
#native $dictionary.add_elem()       dict_add_elem
native $dictionary.del()             dict_del
#native $dictionary.del_elem()       dict_del_elem
native $dictionary.contains()        dict_contains
native $network.hostname()           hostname
native $network.ip()                 ip
native $list.length()                listlen
native $list.subrange()              sublist
native $list.insert()                insert
native $list.replace()               replace
native $list.delete()                delete
native $list.setadd()                setadd
native $list.setremove()             setremove
native $list.union()                 union
native $list.join()                  join
native $list.sort()                  sort
native $list.sorted_index()          sorted_index
native $list.sorted_insert()         sorted_insert
native $list.sorted_delete()         sorted_delete
native $list.sorted_validate()       sorted_validate
native $string.length()              strlen
native $string.subrange()            substr
native $string.explode()             explode
native $string.pad()                 pad
native $string.match_begin()         match_begin
native $string.match_template()      match_template
native $string.match_pattern()       match_pattern
native $string.match_regexp()        match_regexp
native $string.regexp()              regexp
native $string.sed()                 strsed
native $string.replace()             strsub
native $string.crypt()               crypt
native $string.uppercase()           uppercase
native $string.lowercase()           lowercase
native $string.capitalize()          capitalize
native $string.compare()             strcmp
native $string.format()              strfmt
native $string.trim()                trim
native $string.split()               split
native $string.word()                word
native $string.dbquote_explode()     dbquote_explode
native $sys.next_objnum()            next_objnum
native $sys.status()                 status
native $sys.version()                version
native $time.format()                strftime
native $integer.and()                and
native $integer.or()                 or
native $integer.xor()                xor
native $integer.shleft()             shleft
native $integer.shright()            shright
native $integer.not()                not

objs cdc.o cdc_buffer.o cdc_dict.o cdc_list.o cdc_misc.o cdc_string.o cdc_integer.o
