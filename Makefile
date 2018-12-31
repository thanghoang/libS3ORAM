.PHONY: clean All

All:
	@echo "----------Building project:[ libS3ORAM - Debug ]----------"
	@cd "S3ORAM" && "$(MAKE)" -f  "libS3ORAM.mk"
clean:
	@echo "----------Cleaning project:[ libS3ORAM - Debug ]----------"
	@cd "S3ORAM" && "$(MAKE)" -f  "libS3ORAM.mk" clean
