# ls — Custom File Listing Utility

A custom implementation of the Unix `ls` command written in C, supporting long format listing, recursive traversal, hidden files, and more.

## Building

```bash
gcc -o ls ls.c
```

## Usage

```
./ls [options] [file/directory ...]
```

If no file or directory is provided, the current directory (`.`) is listed.

## Options

| Flag | Description |
|------|-------------|
| `-a` | Show hidden files (those beginning with `.`) |
| `-l` | Long listing format — shows permissions, links, owner, group, size, date, and name |
| `-R` | Recursively list subdirectories |
| `-n` | Count files only; suppresses output and prints a total count at the end |
| `-h` | Human-readable file sizes (e.g. `1.2M`, `4.0K`) when used with `-l` |
| `--help` | Display help message and exit |

## Examples

```bash
# List current directory
./ls

# List all files including hidden
./ls -a

# Long format listing
./ls -l /home/user

# Recursive listing with long format
./ls -lR ./projects

# Count files in a directory
./ls -n ./documents

# Long format with human-readable sizes
./ls -lh
```

## Output Format (`-l`)

```
<type><permissions> <links> <owner>   <group>    <size>  <modified>    <name>
-rwxr-xr-x         1       alice     staff      12345   Jan 15 14:32  myfile
drwxr-xr-x         3       alice     staff       4096   Feb  1 09:00  mydir/
```

- Directories are listed with a trailing `/`
- Symbolic links are shown as `linkname -> target`

## Exit Codes

| Code | Meaning |
|------|---------|
| `0`  | Success |
| `64` | An error occurred |
| `72` | File not found |
| `80` | Permission denied |
| `88` | File not found and permission denied |
| `96` | User/group lookup failure |
