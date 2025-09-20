#!/bin/bash
# Manual test script for version history functionality
# This runs outside the test suite so version history is enabled

set -e

echo "🧪 Testing Version History System..."

# Clean up any existing test files
rm -rf /tmp/version_test.md
rm -rf ~/.cache/gtktext/versions/version_test.md-*.version

# Enable version history
export GSETTINGS_SCHEMA_DIR=./data
gsettings set org.gtk.gtktext version-history-enabled true
gsettings set org.gtk.gtktext version-history-max-versions 5

echo "✅ Version history enabled with max 5 versions"

# Create initial test document
echo "# Version Test Document

This is version 1 of the test document." > /tmp/version_test.md

echo "📄 Created initial test document"

# Test 1: Open, edit, and save to create first version
echo "🔄 Test 1: Creating first version through save operation..."

# Use a simple test that loads a document, modifies it, and saves it
cat > /tmp/test_version_history.c << 'EOF'
#include <gtk/gtk.h>
#include <adwaita.h>
#include "src/document/document_manager.c"

static gboolean test_version_creation(gpointer data) {
    GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
    GtkWindow *window = GTK_WINDOW(gtk_window_new());

    DocumentManager *dm = document_manager_new(buffer, window);

    // Open the test file
    GError *error = NULL;
    if (!document_manager_open_file(dm, "/tmp/version_test.md", &error)) {
        g_critical("Failed to open file: %s", error ? error->message : "Unknown error");
        return FALSE;
    }

    // Modify content
    gtk_text_buffer_set_text(buffer, "# Version Test Document\n\nThis is version 2 - modified content.", -1);

    // Save to create version history
    if (!document_manager_save(dm, FALSE, NULL, NULL)) {
        g_critical("Failed to save file");
        return FALSE;
    }

    g_message("Version history test completed successfully");

    // Cleanup
    g_object_unref(dm);
    gtk_window_destroy(window);
    g_object_unref(buffer);

    gtk_main_quit();
    return FALSE;
}

int main(int argc, char *argv[]) {
    gtk_init();
    adw_init();

    g_idle_add(test_version_creation, NULL);
    gtk_main();

    return 0;
}
EOF

# Build the test
echo "🔨 Building version history test..."
gcc -o /tmp/test_version_history /tmp/test_version_history.c \
    $(pkg-config --cflags --libs gtk4 libadwaita-1) \
    -I./include -DHAVE_CONFIG_H

# Run the test
echo "▶️  Running version history test..."
GSETTINGS_SCHEMA_DIR=./data /tmp/test_version_history

# Check if version files were created
echo "🔍 Checking for created version files..."
VERSION_DIR="$HOME/.cache/gtktext/versions"
if [ -d "$VERSION_DIR" ]; then
    VERSION_FILES=$(find "$VERSION_DIR" -name "version_test.md-*.version" 2>/dev/null || echo "")
    if [ -n "$VERSION_FILES" ]; then
        echo "✅ Version files created:"
        ls -la "$VERSION_DIR"/version_test.md-*.version
        echo ""
        echo "📋 Version file content sample:"
        head -10 "$VERSION_DIR"/version_test.md-*.version | head -1
    else
        echo "❌ No version files found in $VERSION_DIR"
        echo "Contents of version directory:"
        ls -la "$VERSION_DIR" || echo "Directory doesn't exist"
    fi
else
    echo "❌ Version directory not created: $VERSION_DIR"
fi

# Test the API functions directly
echo ""
echo "🧪 Testing version history API..."

cat > /tmp/test_version_api.c << 'EOF'
#include <gtk/gtk.h>
#include <glib.h>
#include <stdio.h>

// Include the implementation to test the functions
#include "src/document/document_manager.c"

int main() {
    // Test version listing
    gchar **versions = document_manager_list_version_history("/tmp/version_test.md");
    if (versions) {
        printf("Found %d version files:\n", g_strv_length(versions));
        for (int i = 0; versions[i]; i++) {
            printf("  %d: %s\n", i+1, versions[i]);

            // Test display name
            gchar *display_name = document_manager_get_version_display_name(versions[i]);
            printf("     Display: %s\n", display_name);
            g_free(display_name);
        }
        g_strfreev(versions);
    } else {
        printf("No version files found for /tmp/version_test.md\n");
    }

    return 0;
}
EOF

# Build and run API test
gcc -o /tmp/test_version_api /tmp/test_version_api.c \
    $(pkg-config --cflags --libs glib-2.0) \
    -I./include -DHAVE_CONFIG_H

echo "📊 Version API test results:"
GSETTINGS_SCHEMA_DIR=./data /tmp/test_version_api

# Cleanup
rm -f /tmp/test_version_history /tmp/test_version_history.c
rm -f /tmp/test_version_api /tmp/test_version_api.c

echo ""
echo "🎯 Manual test to verify version history works:"
echo "1. Enable version history: GSETTINGS_SCHEMA_DIR=./data gsettings set org.gtk.gtktext version-history-enabled true"
echo "2. Open a document with: GSETTINGS_SCHEMA_DIR=./data ./builddir/src/gtktext /tmp/version_test.md"
echo "3. Make changes and save (Ctrl+S)"
echo "4. Check for version files in: ~/.cache/gtktext/versions/"
echo "5. Repeat steps 3-4 to create multiple versions"

echo ""
echo "✅ Version history test script completed!"