"""Strip native internal-error prose, retaining original source/file/line.
Only the format string of internal() is changed. Arguments still evaluate,
assertions still fire, and error.c still prints INTERNAL ERROR and file:line.
Ordinary user-facing errors and all original C source are unchanged.
"""
import re
_string=r'"(?:\\.|[^"\\])*"'
_pattern=re.compile(r'/\*.*?\*/|//[^\n]*|'+_string+r"|'(?:\\.|[^'\\])*'|\binternal\s*\(\s*(?:"+_string+r'\s*)+',re.S)
def compact_diagnostics(text):
 count=0;saved=0
 def replace(m):
  nonlocal count,saved
  token=m.group()
  if not token.startswith('internal'):return token
  parts=re.findall(_string,token)
  count+=1
  saved+=sum(len(bytes(s[1:-1],'latin1').decode('unicode_escape')) for s in parts)
  prefix=token[:token.index('(')+1]
  return prefix+'""'+('\n'*(token.count('\n')-prefix.count('\n')))
 result=_pattern.sub(replace,text)
 return result,count,saved
