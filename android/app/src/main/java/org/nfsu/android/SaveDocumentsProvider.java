package org.nfsu.android;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.provider.DocumentsContract.Root;
import android.provider.DocumentsProvider;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.nio.file.Files;

/** Ordinary file access to the existing profile folder; no migration or copies. */
public final class SaveDocumentsProvider extends DocumentsProvider {
    private static final String AUTHORITY = "org.nfsu.android.saves";
    private static final String ROOT = "saves";
    private static final String[] ROOT_COLUMNS = {Root.COLUMN_ROOT_ID, Root.COLUMN_DOCUMENT_ID,
            Root.COLUMN_TITLE, Root.COLUMN_SUMMARY, Root.COLUMN_FLAGS, Root.COLUMN_ICON};
    private static final String[] DOCUMENT_COLUMNS = {Document.COLUMN_DOCUMENT_ID,
            Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE,
            Document.COLUMN_LAST_MODIFIED, Document.COLUMN_FLAGS};

    @Override public boolean onCreate() { return true; }

    @Override public Cursor queryRoots(String[] projection) {
        MatrixCursor result = new MatrixCursor(projection != null ? projection : ROOT_COLUMNS);
        MatrixCursor.RowBuilder row = result.newRow();
        row.add(Root.COLUMN_ROOT_ID, ROOT);
        row.add(Root.COLUMN_DOCUMENT_ID, ROOT);
        row.add(Root.COLUMN_TITLE, "NFS Underground");
        row.add(Root.COLUMN_SUMMARY, getContext().getString(R.string.save_documents_summary));
        row.add(Root.COLUMN_FLAGS, Root.FLAG_SUPPORTS_CREATE | Root.FLAG_LOCAL_ONLY);
        row.add(Root.COLUMN_ICON, R.mipmap.ic_launcher);
        return result;
    }

    @Override public Cursor queryDocument(String id, String[] projection) throws FileNotFoundException {
        MatrixCursor result = documents(projection);
        try {
            addDocument(result, id, resolve(id));
            return result;
        } catch (IOException exception) { throw failure(exception); }
    }

    @Override public Cursor queryChildDocuments(String parentId, String[] projection, String sortOrder)
            throws FileNotFoundException {
        MatrixCursor result = documents(projection);
        try {
            File parent = resolve(parentId);
            File[] children = parent.listFiles();
            if (children == null && !(ROOT.equals(parentId) && !parent.exists()))
                throw new FileNotFoundException("Cannot list save directory");
            if (children != null) {
                for (File child : children) {
                    if (Files.isSymbolicLink(child.toPath())) continue;
                    String id = parentId + "/" + child.getName();
                    addDocument(result, id, resolve(id));
                }
            }
            result.setNotificationUri(getContext().getContentResolver(),
                    DocumentsContract.buildChildDocumentsUri(AUTHORITY, parentId));
            return result;
        } catch (IOException exception) { throw failure(exception); }
    }

    private MatrixCursor documents(String[] projection) {
        return new MatrixCursor(projection != null ? projection : DOCUMENT_COLUMNS);
    }

    private void addDocument(MatrixCursor cursor, String id, File file) throws IOException {
        boolean root = ROOT.equals(id);
        if (!root && !file.exists()) throw new FileNotFoundException("Missing save document");
        boolean directory = root || file.isDirectory();
        int flags = directory ? Document.FLAG_DIR_SUPPORTS_CREATE : Document.FLAG_SUPPORTS_WRITE;
        if (!root) flags |= Document.FLAG_SUPPORTS_DELETE | Document.FLAG_SUPPORTS_RENAME;
        MatrixCursor.RowBuilder row = cursor.newRow();
        row.add(Document.COLUMN_DOCUMENT_ID, id);
        row.add(Document.COLUMN_DISPLAY_NAME, root
                ? getContext().getString(R.string.save_documents_saves) : file.getName());
        row.add(Document.COLUMN_MIME_TYPE, directory ? Document.MIME_TYPE_DIR : "application/octet-stream");
        row.add(Document.COLUMN_SIZE, directory ? null : file.length());
        row.add(Document.COLUMN_LAST_MODIFIED, file.lastModified());
        row.add(Document.COLUMN_FLAGS, flags);
    }

    /** IDs are relative paths. Reject traversal and links, including linked ancestors. */
    private File resolve(String id) throws IOException {
        File runtime = getContext().getExternalFilesDir(null);
        if (runtime == null) runtime = getContext().getFilesDir();
        File root = new File(runtime.getCanonicalFile(), "save/UserData/45410047");
        if (!root.equals(root.getCanonicalFile())) throw new IOException("Linked save directory");
        if (ROOT.equals(id)) return root;
        if (id == null || !id.startsWith(ROOT + "/")) throw new FileNotFoundException("Unknown document");
        File file = root;
        for (String part : id.substring(ROOT.length() + 1).split("/", -1)) {
            validateName(part);
            file = new File(file, part);
            if (Files.isSymbolicLink(file.toPath())) throw new IOException("Linked save document");
        }
        if (!file.getCanonicalFile().toPath().startsWith(root.toPath()))
            throw new IOException("Document outside save directory");
        return file;
    }

    private void validateName(String name) throws IOException {
        if (name == null || name.isEmpty() || name.equals(".") || name.equals(".."))
            throw new IOException("Invalid file name");
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            if (c < 32 || c == 127 || c == '/' || c == '\\') throw new IOException("Invalid file name");
        }
    }

    @Override public boolean isChildDocument(String parent, String child) {
        try {
            resolve(parent);
            resolve(child);
            return child.startsWith(parent + "/");
        } catch (IOException exception) { return false; }
    }

    @Override public ParcelFileDescriptor openDocument(String id, String mode, CancellationSignal signal)
            throws FileNotFoundException {
        if (signal != null) signal.throwIfCanceled();
        try {
            File file = resolve(id);
            if (!file.isFile()) throw new FileNotFoundException("Not a save file");
            return ParcelFileDescriptor.open(file, ParcelFileDescriptor.parseMode(mode));
        } catch (IOException exception) { throw failure(exception); }
    }

    @Override public String createDocument(String parentId, String mimeType, String name) throws FileNotFoundException {
        try {
            validateName(name);
            File parent = resolve(parentId);
            if (ROOT.equals(parentId)) Files.createDirectories(parent.toPath());
            boolean directory = Document.MIME_TYPE_DIR.equals(mimeType);
            String candidate = name;
            for (int number = 1; containsName(parent, candidate); number++) {
                int dot = directory ? -1 : name.lastIndexOf('.');
                candidate = dot > 0 ? name.substring(0, dot) + " (" + number + ")" + name.substring(dot)
                        : name + " (" + number + ")";
            }
            File file = new File(parent, candidate);
            if (directory) Files.createDirectory(file.toPath());
            else Files.createFile(file.toPath());
            changed(parentId);
            return parentId + "/" + candidate;
        } catch (IOException exception) { throw failure(exception); }
    }

    private boolean containsName(File parent, String name) throws IOException {
        String[] names = parent.list();
        if (names == null) throw new IOException("Cannot list save directory");
        for (String existing : names) if (existing.equalsIgnoreCase(name)) return true;
        return false;
    }

    @Override public String renameDocument(String id, String name) throws FileNotFoundException {
        try {
            if (ROOT.equals(id)) throw new IOException("Cannot rename save root");
            validateName(name);
            File file = resolve(id);
            if (name.equals(file.getName())) return id;
            if (containsName(file.getParentFile(), name)) throw new IOException("Name already exists");
            Files.move(file.toPath(), new File(file.getParentFile(), name).toPath());
            String parent = id.substring(0, id.lastIndexOf('/'));
            changed(parent);
            return parent + "/" + name;
        } catch (IOException exception) { throw failure(exception); }
    }

    @Override public void deleteDocument(String id) throws FileNotFoundException {
        try {
            if (ROOT.equals(id)) throw new IOException("Cannot delete save root");
            deleteTree(resolve(id));
            changed(id.substring(0, id.lastIndexOf('/')));
        } catch (IOException exception) { throw failure(exception); }
    }

    private void deleteTree(File file) throws IOException {
        if (!Files.isSymbolicLink(file.toPath()) && file.isDirectory()) {
            File[] children = file.listFiles();
            if (children == null) throw new IOException("Cannot list save directory");
            for (File child : children) deleteTree(child);
        }
        Files.delete(file.toPath());
    }

    private void changed(String parentId) {
        getContext().getContentResolver().notifyChange(
                DocumentsContract.buildChildDocumentsUri(AUTHORITY, parentId), null);
    }

    private FileNotFoundException failure(IOException exception) {
        FileNotFoundException result = new FileNotFoundException(exception.getMessage());
        result.initCause(exception);
        return result;
    }
}
