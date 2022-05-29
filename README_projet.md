
ACHKIK Chaïmae   3804488,
CIOCAN Clara     3801875,
ENGELKE Timon    21137135

README
======


Étape 1 : écriture
------------------

## Chaînage des blocs d'index

Une version d'un fichier ouichefs est représentée par une structure ouichefs_file_index_block contenant un tableau des numéros de blocs composant la version. On ajoute à cette structure deux champs permettant de chaîner les versions entre elles :
    - uint32_t own_block_number : le numéro du bloc index de la version actuelle.
	- uint32_t previous_block_number : le numéro du bloc index de la version précedente.

Le choix de numéros de blocs comme méthode de chaînage est a été effectué afin d'éviter l'utilisation de pointeurs, qui deviennent invalides après chaque redémarrage du système, impliquée par l'utilisation de la structure list_head. La sauvegarde des numéros de blocs d'index nous permet de tracer les numéros de blocs index des versions précedentes directemente en mémoire physique.

## Modification des fonctions d'écriture

    Afin de permettre la sauvegarde des versions précedentes du fichier avant chaque modification d'écriture, on a décidé de modifier dierctement la fonction ouichefs_write_begin comme suit :

```C
static int ouichefs_write_begin(struct file *file,
				struct address_space *mapping, loff_t pos,
				unsigned int len, unsigned int flags,
				struct page **pagep, void **fsdata)
{
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(file->f_inode->i_sb);
	int err;
	struct inode *inode = file->f_inode;
	uint32_t nr_allocs = 0;
	struct buffer_head *bh_index, *bh_new_index;
	struct buffer_head *bh_new_data_block, *bh_data_block;
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct super_block *sb = inode->i_sb;
	struct ouichefs_file_index_block *index, *new_index;
	uint32_t new_index_no, new_block_no;
	int i;

    ... // Pas de changement

	/* Duplicate index block and data */
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index)
		return -EIO;
	index = (struct ouichefs_file_index_block *)bh_index->b_data;

	new_index_no = get_free_block(sbi);
	if (!new_index_no)
		return -ENOSPC;

	bh_new_index = sb_bread(sb, new_index_no);
	new_index = (struct ouichefs_file_index_block *)bh_new_index->b_data;
	memset(new_index, 0, sizeof(*new_index));

    // Parcours de la liste des blocs de la table d'index
	for (i = 0; i < sizeof(index->blocks) / sizeof(uint32_t); i++) {
		if (index->blocks[i] == 0)
			continue;

		new_block_no = get_free_block(sbi);
		if (!new_block_no)
			return -ENOSPC;

		bh_new_data_block = sb_bread(sb, new_block_no);
		bh_data_block = sb_bread(sb, index->blocks[i]);
		memcpy(bh_new_data_block->b_data, bh_data_block->b_data,
				OUICHEFS_BLOCK_SIZE); // Copie des données des blocs
		mark_buffer_dirty(bh_new_data_block);
		sync_dirty_buffer(bh_new_data_block);
		brelse(bh_new_data_block);
		brelse(bh_data_block);
		new_index->blocks[i] = new_block_no;
	}

	new_index->own_block_number = new_index_no;
	new_index->previous_block_number = ci->index_block;
	ci->index_block = new_index_no;
	ci->last_index_block = new_index_no;
	mark_buffer_dirty(bh_new_index);
	mark_buffer_dirty(bh_index);
	sync_dirty_buffer(bh_new_index);
	sync_dirty_buffer(bh_index);
    // Demande de liberation des buffer_head
	brelse(bh_index);
	brelse(bh_new_index);

    ... // Pas de changement
}
```

    Le reste des fonctions d'écriture n'ont pas subis de changements notables.


Étape 2 : utilitaire de déboguage
---------------------------------

## Fichier du debugfs fonctionnel

    Les fonctionnalités de l'utilitatire de déboguage ont été implémentées dans le fichier fs.c comme suit :

+ Fichiers à include :
```C
...
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/buffer_head.h>
...
```


+ Ajout du champs  : struct dentry *debug_file à la fin de la structure ouichefs_sb_info. Ce champs va nous permettre de récupérer facilement les informations concernant le fichier debug ainsi que sa destruction.
```C
// Fonction d'affichage de données
static int debug_seq_show(struct seq_file *file, void *v)
{
	int ino;
	struct super_block *sb = (struct super_block *)file->private;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct inode *inode;
	struct ouichefs_inode_info *ci;
	uint32_t first_block_no, cur_block_no;
	struct buffer_head *bh;
	int count;
	struct ouichefs_file_index_block *index;
	char c[100];

	for (ino = 0; ino < sbi->nr_inodes; ino++) {
		inode = ouichefs_iget(sb, ino);
		ci = OUICHEFS_INODE(inode);

		if (inode->i_nlink == 0)
			goto iput;

		if (!S_ISREG(inode->i_mode))
			goto iput;

		count = 0;

		memset(c, 0, sizeof(c));

		first_block_no = ci->last_index_block;
		cur_block_no = first_block_no;

		while (cur_block_no != 0) {
			count++;
			snprintf(c, 100, "%s, %d", c, cur_block_no);

			bh = sb_bread(sb, cur_block_no);
			if (!bh)
				continue;

			index = (struct ouichefs_file_index_block *)bh->b_data;
			cur_block_no = index->previous_block_number;


		}
		seq_printf(file, "%d %d [%s]\n", ino, count, c + 2);
iput:
		iput(inode);
	}
	return 0;
}
```


```C
// Fonction open. Utilisation de single_open.
static int debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_seq_show, inode->i_private);
}

static const struct file_operations debug_fops = {
	.owner = THIS_MODULE,
	.open = debugfs_open,
	.read = seq_read,
	.release = single_release,
};
```
```C
int ouichefs_debug_file(struct super_block *sb)
{
	struct dentry *debugfs_file;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);

	debugfs_file = debugfs_create_file(sb->s_id, 0400, NULL, NULL,
					&debug_fops);
	debugfs_file->d_inode->i_private = sb;
	if (!debugfs_file) {
		pr_err("Debugfs file creation failed\n");
		return -EIO;
	}
	sbi->debug_file = debugfs_file;
	return 0;
}
```

+ Ajout de la création du fichier lors du 'mount' (monter le système fichiers) :

```C
struct dentry *ouichefs_mount(struct file_system_type *fs_type, int flags,
			      const char *dev_name, void *data)
{
    ...
    ouichefs_debug_file(dentry->d_inode->i_sb); // Creation du fichier debugfs
    return dentry;
}
```
+ Destruction du fichier debugfs quand on démonte (unmount) le système fichiers :
```C
/*
 * Unmount a ouiche_fs partition
 */
void ouichefs_kill_sb(struct super_block *sb)
{
    struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
    debugfs_remove(sbi->debug_file); // Destruction du fichier debugfs

    kill_block_super(sb);
    ...
}
```

Étape 3 : vue courante
----------------------

## Modification de la structure ouichefs_inode

    Le champs uint32_t last_index_block a été ajouté à la fin de la structure ouichefs_inode pour indiquer la dernière version du fichier.

## Requête ioctl changement vue courante

    Implémentation de la requête ioctl pour le changement de la vue courante :

+ Dans le fichier ouichefs.h, ajout du define suivant pour la definition de la requete  :
```C
         #define OUICHEFS_SHOW_VERSION	_IOR('O', 0, unsigned long)

```

+ Implémentation de la fonction ouichefs_change_file_version dans le fichier file.c qui prend en parametre le fichier dont on veut changer de version et un entier indiquant la version souhaitée, et qui retourne un entier indiquant si le changement de version a bien réussit.
```C
        int ouichefs_change_file_version(struct file *file, int version)
        {
            struct super_block *sb = file->f_inode->i_sb;
            struct ouichefs_file_index_block *index_block;
            struct buffer_head *bh;
            struct ouichefs_inode_info *info = OUICHEFS_INODE(file->f_inode);
            uint32_t current_version_block = info->last_index_block;

            while (version > 0) {
                bh = sb_bread(sb, current_version_block);
                index_block = (struct ouichefs_file_index_block *)bh->b_data;
                current_version_block = index_block->previous_block_number;
                version--;
                if (current_version_block == 0)
                    return -EINVAL;
            }
            info->index_block = current_version_block;

            mark_inode_dirty(file->f_inode);
            invalidate_mapping_pages(file->f_inode->i_mapping, 0, -1); // Invalidation de la cache
            return 0;
        }
```

        Pour cette partie, une invalidation de cache s'est avérée nécessaire afin d'obliger la mise à jour des données à partir du disque et éviter la lecture de versions obsolètes à partir de la cache après un passage à une version antèrieure.

+ Implémentation de la fonction ioctl :
 ```C
        long ouichefs_ioctl(struct file *file, unsigned int cmd, unsigned long argp)
        {
            switch (cmd) {
            case (OUICHEFS_SHOW_VERSION):
                return ouichefs_change_file_version(file, argp);
            default:
                return -EINVAL;
            }
            return 0;
        }
```


## Modification des fonctions d'écriture

    La principale modification apportée à la fonction d'écriture est l'ajout d'une vérification au début de la fonction afin de s'assurer que la version courante est la dernière version avant chaque écriture, càd que index_block vaut bien last_index_block. Si ce n'est pas le cas, on retourne une erreur et on effectue pas l'écriture.

```C
static int ouichefs_write_begin(struct file *file,
				struct address_space *mapping, loff_t pos,
				unsigned int len, unsigned int flags,
				struct page **pagep, void **fsdata)
{
    ... // Pas de changement
    if (ci->index_block != ci->last_index_block) {
        pr_err("Unable to write to old version!\n");
        return -EINVAL;
    }
    ... // Pas de changements
}
```

Étape 4 : restauration
----------------------

## Requête ioctl restauration

    + Definition de la requete  : #define OUICHEFS_RESTORE_VERSION	_IO('O', 1)
    + Définition de la fonction ouichefs_restore_file_version qui permet la restauration d'une version prècedente comme version actuelle et libère les blocs non inutilisés.

```C
int ouichefs_restore_file_version(struct file *file)
{
	struct ouichefs_inode_info *info = OUICHEFS_INODE(file->f_inode);
	struct buffer_head *bh_index;
	struct super_block *sb = file->f_inode->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct ouichefs_file_index_block *index;
	uint32_t current_version_block = info->last_index_block;
	int i;

	while (current_version_block != info->index_block) {
		bh_index = sb_bread(sb, current_version_block);
		if (!bh_index)
			return -EIO;
		index = (struct ouichefs_file_index_block *)bh_index->b_data;

		for (i = 0; i < sizeof(index->blocks) / sizeof(uint32_t); i++) {
			if (index->blocks[i] == 0)
				continue;
			put_block(sbi, index->blocks[i]);
		}
		put_block(sbi, current_version_block);
		current_version_block = index->previous_block_number;
		brelse(bh_index);
	}
	info->last_index_block = info->index_block;
	return 0;
}
```
## Modification fonction ioctl

```C
long ouichefs_ioctl(struct file *file, unsigned int cmd, unsigned long argp)
{
	switch (cmd) {
	case (OUICHEFS_SHOW_VERSION):
		return ouichefs_change_file_version(file, argp);
	case (OUICHEFS_RESTORE_VERSION): // Nouvelle requête
		return ouichefs_restore_file_version(file);
	default:
		return -EINVAL;
	}
	return 0;
}
```

+ Dans le fichier ouichefs.h, ajout du define suivant pour la definition de la requete  :
```C
    #define OUICHEFS_RESTORE_VERSION	_IO('O', 1)
```
## Blocs libérés utilisables

    On rend les blocs libérés utilisables dans la fonction ouichefs_restore_file_version en effectuant un 'put_block' de chaque bloc libéré du tableau de blocs afin de signaler dans le bitmap du superbloc qu'il est libre, et donc utilisable, avant d'effectuer un 'put_block' du bloc index de la version libérée.


Étape 5 : déduplication
-----------------------

## Modification des fonctions d'écriture

---RÉPONSES---

## Libération synchrone

---RÉPONSES---

## Libération asynchrone

---RÉPONSES---
