
xml_file=$(LOCAL_PATH)/$(SUB_PATH)/$(FILE_NAME)

ifeq ($(shell test -f $(xml_file).xml && echo found), found)

$(info $(shell cp $(xml_file).xml $(xml_file)_temp.xml -f))

ifeq ($(shell grep -c media_codecs_c2_ac3.xml ${xml_file}_temp.xml), 0)
ifeq ($(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_ac3_dec/media_codecs_c2_ac3.xml && echo found), found)
    $(info $(shell sed -i '/<\/Included>/i\    <Include href=\"media_codecs_c2_ac3.xml\" />' $(xml_file)_temp.xml))
endif
endif

ifeq ($(shell grep -c media_codecs_c2_ddp.xml ${xml_file}_temp.xml), 0)
ifeq ($(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_ddp_dec/media_codecs_c2_ddp.xml && echo found), found)
    $(info $(shell sed -i '/<\/Included>/i\    <Include href=\"media_codecs_c2_ddp.xml\" />' $(xml_file)_temp.xml))
endif
endif

ifeq ($(shell grep -c media_codecs_c2_ms.xml ${xml_file}_temp.xml), 0)
ifeq ($(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_ms_codec/media_codecs_c2_ms.xml && echo found), found)
    $(info $(shell sed -i '/<\/Included>/i\    <Include href=\"media_codecs_c2_ms.xml\" />' $(xml_file)_temp.xml))
endif
endif

ifneq ($(findstring $(BOARD_SOC_TYPE),  IMX8MQ IMX8Q),)
ifeq ($(shell grep -c media_codecs_c2_wmv9.xml ${xml_file}_temp.xml), 0)
ifeq ($(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_ms_codec/media_codecs_c2_wmv9.xml && echo found), found)
    $(info $(shell sed -i '/<\/Included>/i\    <Include href=\"media_codecs_c2_wmv9.xml\" />' $(xml_file)_temp.xml))
endif
endif
endif

ifeq ($(shell grep -c media_codecs_c2_ra.xml ${xml_file}_temp.xml), 0)
ifeq ($(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_real_dec/media_codecs_c2_ra.xml && echo found), found)
    $(info $(shell sed -i '/<\/Included>/i\    <Include href=\"media_codecs_c2_ra.xml\" />' $(xml_file)_temp.xml))
endif
endif

ifneq ($(findstring $(BOARD_SOC_TYPE),  IMX8MQ IMX8Q),)
ifeq ($(shell grep -c media_codecs_c2_rv.xml ${xml_file}_temp.xml), 0)
ifeq ($(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_real_dec/media_codecs_c2_rv.xml && echo found), found)
    $(info $(shell sed -i '/<\/Included>/i\    <Include href=\"media_codecs_c2_rv.xml\" />' $(xml_file)_temp.xml))
endif
endif
endif

else
    $(warning can't find $(FILE_NAME).xml)
endif

