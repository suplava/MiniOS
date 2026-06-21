with open('viz/bridge.py', 'r') as f:
    lines = f.readlines()

BS = chr(92)  # backslash

# Fix line 103 (index 102) - the broadcaster debug line
lines[102] = '            sys.stderr.write("[bridge] BC: " + str(evt.get("type","?")) + "' + BS + 'n")\n'
# Delete line 104 (index 103) - continuation
del lines[103]

# Find and fix the mini_reader debug line (around line 150+)
for i, l in enumerate(lines):
    if 'sys.stderr.write("[bridge] Q:' in l:
        lines[i] = '        sys.stderr.write("[bridge] Q: " + str(evt.get("type","?")) + "' + BS + 'n")\n'
        if i+1 < len(lines) and lines[i+1].strip().startswith('")'):
            del lines[i+1]
        break

with open('viz/bridge.py', 'w') as f:
    f.writelines(lines)
print('Fixed')
