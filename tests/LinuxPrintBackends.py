"""Check system GTK printing backends and the bundled PDF writer."""

import argparse
import ctypes as C
import ctypes.util
from pathlib import Path
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    args = parser.parse_args()
    package = args.package.resolve(strict=True)
    gtk_library = ctypes.util.find_library("gtk-3")
    if not gtk_library:
        raise RuntimeError("The system GTK3 runtime is absent")
    gtk = C.CDLL(gtk_library, mode=C.RTLD_GLOBAL)
    gtk.gtk_init_check.argtypes = [C.c_void_p, C.c_void_p]
    if not gtk.gtk_init_check(None, None):
        raise RuntimeError("GTK cannot initialize its native printer interface")
    callback_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p)
    gtk.gtk_printer_is_virtual.argtypes = [C.c_void_p]
    gtk.gtk_printer_accepts_pdf.argtypes = [C.c_void_p]
    printers = []

    @callback_type
    def printer_found(printer, _):
        virtual_pdf = bool(gtk.gtk_printer_is_virtual(printer) and gtk.gtk_printer_accepts_pdf(printer))
        printers.append(virtual_pdf)
        return virtual_pdf

    gtk.gtk_enumerate_printers.argtypes = [callback_type, C.c_void_p, C.c_void_p, C.c_int]
    gtk.gtk_enumerate_printers.restype = None
    gtk.gtk_enumerate_printers(printer_found, None, None, True)
    if not any(printers):
        raise RuntimeError("GTK did not expose its Print to File PDF printer")
    backends = {path.resolve() for directory in ("/usr/lib", "/usr/lib64")
                for path in Path(directory).glob("**/gtk-3.0/*/printbackends/libprintbackend-cups.so")}
    if not backends:
        raise RuntimeError("The GTK CUPS print backend is absent")
    for path in backends:
        C.CDLL(str(path))

    library = next((package / "Bin").glob("libhpdf*.so*"), None)
    if library is None:
        raise RuntimeError("The packaged PDF writer is absent")
    pdf = C.CDLL(str(library))
    pdf.HPDF_New.argtypes = [C.c_void_p, C.c_void_p]
    pdf.HPDF_New.restype = C.c_void_p
    pdf.HPDF_AddPage.argtypes = [C.c_void_p]
    pdf.HPDF_AddPage.restype = C.c_void_p
    pdf.HPDF_SaveToFile.argtypes = [C.c_void_p, C.c_char_p]
    pdf.HPDF_SaveToFile.restype = C.c_ulong
    pdf.HPDF_Free.argtypes = [C.c_void_p]
    pdf.HPDF_Free.restype = None
    document = pdf.HPDF_New(None, None)
    if not document:
        raise RuntimeError("Cannot allocate a printable PDF")
    try:
        if not pdf.HPDF_AddPage(document):
            raise RuntimeError("Cannot add a PDF page")
        with tempfile.TemporaryDirectory(prefix="NeMoDev PDF ") as directory:
            path = Path(directory) / "page.pdf"
            if pdf.HPDF_SaveToFile(document, str(path).encode()) != 0:
                raise RuntimeError("Cannot save the printable PDF")
            contents = path.read_bytes()
            if not contents.startswith(b"%PDF-") or b"/MediaBox" not in contents or b"xref" not in contents:
                raise RuntimeError("The PDF writer did not create a complete page")
    finally:
        pdf.HPDF_Free(document)
    print("System GTK Print to File and CUPS backends load; bundled libharu writes a PDF page")


if __name__ == "__main__":
    main()
