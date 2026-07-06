
Generate flatbuffers schema
```
ob_dump --fbs objectbox-model.json -o schema.fbs
flatc --python -o models/ shema.fbs
```

