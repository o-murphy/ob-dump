import ob_dump_reader as obr


if __name__ == "__main__":
    fixture = "/home/murphy/.local/share/com.o.murphy.ebalistyka"
    obr.read_objectbox_records(fixture, lambda x: print(x))
