include Makefile.vita
run:
	python3 vita/tools/build_performance_profile.py $(PROFILE) --jobs 8
