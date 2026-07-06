import ob_dump_reader as ob


if __name__ == "__main__":
    fixture = "/home/murphy/.local/share/com.o.murphy.ebalistyka"
    ob.read_objectbox_records(fixture, lambda x: print(x))
