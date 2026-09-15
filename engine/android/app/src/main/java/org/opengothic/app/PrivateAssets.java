package org.opengothic.app;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Streaming, restartable extraction; independent of Android for host-side checks. */
public final class PrivateAssets {
    public interface Progress {
        void update(long completed, long total, String path);
    }

    private static final String INDEX = "opengothic-private-v1.tsv";
    private static final int BUFFER = 1024 * 1024;

    private static final class Item {
        long size;
        String hash;
        String path;
    }

    private static MessageDigest digest() {
        try {
            return MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException e) {
            throw new AssertionError(e);
        }
    }

    private static String hex(byte[] value) {
        StringBuilder text = new StringBuilder();
        for (byte b : value) text.append(String.format(Locale.ROOT, "%02x", b & 255));
        return text.toString();
    }

    private static String hash(File file) throws IOException {
        MessageDigest hash = digest();
        try (InputStream in = new FileInputStream(file)) {
            byte[] buffer = new byte[BUFFER];
            int count;
            while ((count = in.read(buffer)) != -1) hash.update(buffer, 0, count);
        }
        return hex(hash.digest());
    }

    private static File destination(File root, String path) throws IOException {
        if (path.contains("\\") || path.contains(":") || path.startsWith("/") || path.endsWith(".og-extract-part") ||
                Arrays.asList(path.split("/", -1)).contains("..") ||
                Arrays.asList(path.split("/", -1)).contains(".") ||
                Arrays.asList(path.split("/", -1)).contains("")) {
            throw new IOException("Unsafe archive path: " + path);
        }
        boolean allowed = path.startsWith("Gothic2/Data/") || path.startsWith("Gothic2/_work/") ||
                path.equals("Gothic2/System/GothicGame.ini") || path.equals("Gothic.ini") ||
                path.matches("save_slot_[0-9]+\\.sav");
        if (!allowed) throw new IOException("Unexpected archive path: " + path);
        File target = new File(root, path);
        if (!target.getCanonicalPath().startsWith(root.getCanonicalPath() + File.separator)) {
            throw new IOException("Archive path escapes game storage: " + path);
        }
        return target;
    }

    public static void extract(InputStream source, File root, File marker, Progress progress) throws IOException {
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Cannot create game storage");
        try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(source, BUFFER))) {
            ZipEntry entry = zip.getNextEntry();
            if (entry == null || !INDEX.equals(entry.getName())) {
                throw new IOException("Choose the game-data.zip produced by the OpenGothic setup scripts");
            }
            ByteArrayOutputStream index = new ByteArrayOutputStream();
            byte[] buffer = new byte[BUFFER];
            int count;
            while ((count = zip.read(buffer)) != -1) {
                if (index.size() + count > 4 * BUFFER) throw new IOException("Asset index is too large");
                index.write(buffer, 0, count);
            }
            String fingerprint = hex(digest().digest(index.toByteArray()));
            LinkedHashMap<String, Item> items = new LinkedHashMap<>();
            long total = 0;
            long needed = 256L * BUFFER;
            boolean complete = marker.isFile() && hash(marker).equals(fingerprint);
            for (String line : new String(index.toByteArray(), StandardCharsets.UTF_8).split("\n")) {
                String[] fields = line.split("\t", -1);
                if (fields.length != 3 || !fields[0].matches("[0-9]{1,12}") ||
                        !fields[1].matches("[0-9a-f]{64}")) throw new IOException("Invalid asset index");
                Item item = new Item();
                item.size = Long.parseLong(fields[0]);
                item.hash = fields[1];
                item.path = fields[2];
                File target = destination(root, item.path);
                if (items.put(item.path, item) != null) throw new IOException("Duplicate asset path");
                total += item.size;
                if (total > 64L * 1024 * BUFFER) throw new IOException("Asset package exceeds 64 GiB");
                if (!target.exists()) needed += item.size;
                if (!target.isFile() || (item.path.startsWith("Gothic2/") && target.length() != item.size)) {
                    complete = false;
                }
            }
            if (items.isEmpty()) throw new IOException("Empty asset index");
            if (complete) return;
            if (root.getUsableSpace() < needed) {
                throw new IOException("Not enough free space. Need at least " + (needed / BUFFER) + " MiB more available for extraction.");
            }
            long done = 0;
            while ((entry = zip.getNextEntry()) != null) {
                Item item = items.remove(entry.getName());
                if (item == null || entry.isDirectory()) throw new IOException("Unexpected or duplicate archive entry");
                File target = destination(root, item.path);
                progress.update(done, total, item.path);
                if (target.exists()) {
                    // Settings and saves are seeds, never replacements for user data.
                    if (item.path.startsWith("Gothic2/") &&
                            (target.length() != item.size || !hash(target).equals(item.hash))) {
                        throw new IOException("Existing game file differs: " + item.path +
                                ". Keep your saves/settings; back up and move Gothic2 before changing installations.");
                    }
                    MessageDigest contentHash = digest();
                    long read = 0;
                    while ((count = zip.read(buffer)) != -1) {
                        read += count;
                        if (read > item.size) throw new IOException("Asset exceeds its declared size");
                        contentHash.update(buffer, 0, count);
                        progress.update(done + read, total, item.path);
                    }
                    if (read != item.size || !hex(contentHash.digest()).equals(item.hash)) {
                        throw new IOException("Asset checksum failed: " + item.path);
                    }
                    done += item.size;
                    continue;
                }
                File parent = target.getParentFile();
                if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create " + parent);
                // A fixed temporary name lets the next launch recover after process death.
                File temporary = new File(parent, target.getName() + ".og-extract-part");
                if (!temporary.getCanonicalPath().equals(target.getCanonicalPath() + ".og-extract-part")) {
                    throw new IOException("Unsafe extraction temporary file");
                }
                MessageDigest contentHash = digest();
                long written = 0;
                try {
                    try (FileOutputStream out = new FileOutputStream(temporary)) {
                        while ((count = zip.read(buffer)) != -1) {
                            written += count;
                            if (written > item.size) throw new IOException("Asset exceeds its declared size");
                            contentHash.update(buffer, 0, count);
                            out.write(buffer, 0, count);
                            progress.update(done + written, total, item.path);
                        }
                        out.getFD().sync();
                    }
                    if (written != item.size || !hex(contentHash.digest()).equals(item.hash)) {
                        throw new IOException("Asset checksum failed: " + item.path);
                    }
                    if (target.exists() || !temporary.renameTo(target)) throw new IOException("Cannot safely install " + item.path);
                } finally {
                    if (temporary.exists() && !temporary.delete()) temporary.deleteOnExit();
                }
                done += item.size;
            }
            if (!items.isEmpty()) throw new IOException("Truncated asset package");
            try (FileOutputStream out = new FileOutputStream(marker)) {
                index.writeTo(out);
                out.getFD().sync();
            }
            progress.update(total, total, "Ready");
        }
    }

    private PrivateAssets() {}
}
