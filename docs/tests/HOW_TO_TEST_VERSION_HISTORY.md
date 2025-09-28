<!-- moved: docs/tests/HOW_TO_TEST_VERSION_HISTORY.md (2025-09-28) -->
# How to Test Version History

## 🎯 Quick Test

1. **Enable version history:**
   ```bash
   GSETTINGS_SCHEMA_DIR=./data gsettings set org.gtk.gtktext version-history-enabled true
   ```

2. **Create test document:**
   ```bash
   echo "# Version Test

   This is version 1." > /tmp/test_versions.md
   ```

3. **Run the application:**
   ```bash
   GSETTINGS_SCHEMA_DIR=./data ./builddir/src/gtktext /tmp/test_versions.md
   ```

4. **Test the workflow:**
   - Make changes to the document
   - Save with Ctrl+S (this creates a version)
   - Make more changes and save again
   - Repeat a few times

5. **Check for version files:**
   ```bash
   ls -la ~/.cache/gtktext/versions/
   ```

   You should see files like: `test_versions.md-20250920-175500.version`

## 🔍 Verification

**Check settings:**
```bash
GSETTINGS_SCHEMA_DIR=./data gsettings get org.gtk.gtktext version-history-enabled
GSETTINGS_SCHEMA_DIR=./data gsettings get org.gtk.gtktext version-history-max-versions
```

**Check version file content:**
```bash
head -20 ~/.cache/gtktext/versions/test_versions.md-*.version
```

## 📋 What Should Happen

1. **On first save:** Version directory is created: `~/.cache/gtktext/versions/`
2. **On each save:** A new `.version` file is created with timestamp
3. **After max versions:** Older versions are automatically deleted
4. **Version files contain:** Metadata (timestamp, original path) + document content

## 🧪 Test Scenarios

### Test 1: Basic Functionality
- Create document → Edit → Save → Check for version file

### Test 2: Multiple Versions
- Create → Save → Edit → Save → Edit → Save
- Should see multiple version files with different timestamps

### Test 3: Max Versions Limit
- Set max to 3: `gsettings set org.gtk.gtktext version-history-max-versions 3`
- Save 5 versions → Should only keep newest 3

### Test 4: Disabled Version History
- Disable: `gsettings set org.gtk.gtktext version-history-enabled false`
- Save document → No version files should be created

## 🚨 Expected Behavior

✅ **What works now:**
- Version files are created on each successful save
- Files stored in `~/.cache/gtktext/versions/`
- Automatic cleanup when max versions exceeded
- Settings control enable/disable and max count

❌ **What doesn't work yet:**
- No UI to browse/restore versions (that would be a future feature)
- The version history system is complete but needs UI for user access

## 🔧 Troubleshooting

**No version files created?**
- Check if version history is enabled
- Check if directory has write permissions
- Look for error messages in debug output

**Run with debug:**
```bash
GSETTINGS_SCHEMA_DIR=./data G_MESSAGES_DEBUG=all ./builddir/src/gtktext /tmp/test_versions.md
```

Look for messages like:
- `Version history saved: /path/to/version/file`
- `Version history disabled` (if disabled in settings)
